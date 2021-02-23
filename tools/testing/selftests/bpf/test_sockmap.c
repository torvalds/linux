// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018 Covalent IO, Inc. http://covalent.io
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <sched.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/sendfile.h>

#include <linux/netlink.h>
#include <linux/socket.h>
#include <linux/sock_diag.h>
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <linux/tls.h>
#include <assert.h>
#include <libgen.h>

#include <getopt.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"
#include "bpf_rlimit.h"
#include "cgroup_helpers.h"

int running;
static void running_handler(int a);

#ifndef TCP_ULP
# define TCP_ULP 31
#endif
#ifndef SOL_TLS
# define SOL_TLS 282
#endif

/* randomly selected ports for testing on lo */
#define S1_PORT 10000
#define S2_PORT 10001

#define BPF_SOCKMAP_FILENAME  "test_sockmap_kern.o"
#define BPF_SOCKHASH_FILENAME "test_sockhash_kern.o"
#define CG_PATH "/sockmap"

/* global sockets */
int s1, s2, c1, c2, p1, p2;
int test_cnt;
int passed;
int failed;
int map_fd[9];
struct bpf_map *maps[9];
int prog_fd[11];

int txmsg_pass;
int txmsg_redir;
int txmsg_drop;
int txmsg_apply;
int txmsg_cork;
int txmsg_start;
int txmsg_end;
int txmsg_start_push;
int txmsg_end_push;
int txmsg_start_pop;
int txmsg_pop;
int txmsg_ingress;
int txmsg_redir_skb;
int txmsg_ktls_skb;
int txmsg_ktls_skb_drop;
int txmsg_ktls_skb_redir;
int ktls;
int peek_flag;
int skb_use_parser;
int txmsg_omit_skb_parser;

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"cgroup",	required_argument,	NULL, 'c' },
	{"rate",	required_argument,	NULL, 'r' },
	{"verbose",	optional_argument,	NULL, 'v' },
	{"iov_count",	required_argument,	NULL, 'i' },
	{"length",	required_argument,	NULL, 'l' },
	{"test",	required_argument,	NULL, 't' },
	{"data_test",   no_argument,		NULL, 'd' },
	{"txmsg",		no_argument,	&txmsg_pass,  1  },
	{"txmsg_redir",		no_argument,	&txmsg_redir, 1  },
	{"txmsg_drop",		no_argument,	&txmsg_drop, 1 },
	{"txmsg_apply",	required_argument,	NULL, 'a'},
	{"txmsg_cork",	required_argument,	NULL, 'k'},
	{"txmsg_start", required_argument,	NULL, 's'},
	{"txmsg_end",	required_argument,	NULL, 'e'},
	{"txmsg_start_push", required_argument,	NULL, 'p'},
	{"txmsg_end_push",   required_argument,	NULL, 'q'},
	{"txmsg_start_pop",  required_argument,	NULL, 'w'},
	{"txmsg_pop",	     required_argument,	NULL, 'x'},
	{"txmsg_ingress", no_argument,		&txmsg_ingress, 1 },
	{"txmsg_redir_skb", no_argument,	&txmsg_redir_skb, 1 },
	{"ktls", no_argument,			&ktls, 1 },
	{"peek", no_argument,			&peek_flag, 1 },
	{"txmsg_omit_skb_parser", no_argument,      &txmsg_omit_skb_parser, 1},
	{"whitelist", required_argument,	NULL, 'n' },
	{"blacklist", required_argument,	NULL, 'b' },
	{0, 0, NULL, 0 }
};

struct test_env {
	const char *type;
	const char *subtest;
	const char *prepend;

	int test_num;
	int subtest_num;

	int succ_cnt;
	int fail_cnt;
	int fail_last;
};

struct test_env env;

struct sockmap_options {
	int verbose;
	bool base;
	bool sendpage;
	bool data_test;
	bool drop_expected;
	int iov_count;
	int iov_length;
	int rate;
	char *map;
	char *whitelist;
	char *blacklist;
	char *prepend;
};

struct _test {
	char *title;
	void (*tester)(int cg_fd, struct sockmap_options *opt);
};

static void test_start(void)
{
	env.subtest_num++;
}

static void test_fail(void)
{
	env.fail_cnt++;
}

static void test_pass(void)
{
	env.succ_cnt++;
}

static void test_reset(void)
{
	txmsg_start = txmsg_end = 0;
	txmsg_start_pop = txmsg_pop = 0;
	txmsg_start_push = txmsg_end_push = 0;
	txmsg_pass = txmsg_drop = txmsg_redir = 0;
	txmsg_apply = txmsg_cork = 0;
	txmsg_ingress = txmsg_redir_skb = 0;
	txmsg_ktls_skb = txmsg_ktls_skb_drop = txmsg_ktls_skb_redir = 0;
	txmsg_omit_skb_parser = 0;
	skb_use_parser = 0;
}

static int test_start_subtest(const struct _test *t, struct sockmap_options *o)
{
	env.type = o->map;
	env.subtest = t->title;
	env.prepend = o->prepend;
	env.test_num++;
	env.subtest_num = 0;
	env.fail_last = env.fail_cnt;
	test_reset();
	return 0;
}

static void test_end_subtest(void)
{
	int error = env.fail_cnt - env.fail_last;
	int type = strcmp(env.type, BPF_SOCKMAP_FILENAME);

	if (!error)
		test_pass();

	fprintf(stdout, "#%2d/%2d %8s:%s:%s:%s\n",
		env.test_num, env.subtest_num,
		!type ? "sockmap" : "sockhash",
		env.prepend ? : "",
		env.subtest, error ? "FAIL" : "OK");
}

static void test_print_results(void)
{
	fprintf(stdout, "Pass: %d Fail: %d\n",
		env.succ_cnt, env.fail_cnt);
}

static void usage(char *argv[])
{
	int i;

	printf(" Usage: %s --cgroup <cgroup_path>\n", argv[0]);
	printf(" options:\n");
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-12s", long_options[i].name);
		if (long_options[i].flag != NULL)
			printf(" flag (internal value:%d)\n",
				*long_options[i].flag);
		else
			printf(" -%c\n", long_options[i].val);
	}
	printf("\n");
}

char *sock_to_string(int s)
{
	if (s == c1)
		return "client1";
	else if (s == c2)
		return "client2";
	else if (s == s1)
		return "server1";
	else if (s == s2)
		return "server2";
	else if (s == p1)
		return "peer1";
	else if (s == p2)
		return "peer2";
	else
		return "unknown";
}

static int sockmap_init_ktls(int verbose, int s)
{
	struct tls12_crypto_info_aes_gcm_128 tls_tx = {
		.info = {
			.version     = TLS_1_2_VERSION,
			.cipher_type = TLS_CIPHER_AES_GCM_128,
		},
	};
	struct tls12_crypto_info_aes_gcm_128 tls_rx = {
		.info = {
			.version     = TLS_1_2_VERSION,
			.cipher_type = TLS_CIPHER_AES_GCM_128,
		},
	};
	int so_buf = 6553500;
	int err;

	err = setsockopt(s, 6, TCP_ULP, "tls", sizeof("tls"));
	if (err) {
		fprintf(stderr, "setsockopt: TCP_ULP(%s) failed with error %i\n", sock_to_string(s), err);
		return -EINVAL;
	}
	err = setsockopt(s, SOL_TLS, TLS_TX, (void *)&tls_tx, sizeof(tls_tx));
	if (err) {
		fprintf(stderr, "setsockopt: TLS_TX(%s) failed with error %i\n", sock_to_string(s), err);
		return -EINVAL;
	}
	err = setsockopt(s, SOL_TLS, TLS_RX, (void *)&tls_rx, sizeof(tls_rx));
	if (err) {
		fprintf(stderr, "setsockopt: TLS_RX(%s) failed with error %i\n", sock_to_string(s), err);
		return -EINVAL;
	}
	err = setsockopt(s, SOL_SOCKET, SO_SNDBUF, &so_buf, sizeof(so_buf));
	if (err) {
		fprintf(stderr, "setsockopt: (%s) failed sndbuf with error %i\n", sock_to_string(s), err);
		return -EINVAL;
	}
	err = setsockopt(s, SOL_SOCKET, SO_RCVBUF, &so_buf, sizeof(so_buf));
	if (err) {
		fprintf(stderr, "setsockopt: (%s) failed rcvbuf with error %i\n", sock_to_string(s), err);
		return -EINVAL;
	}

	if (verbose)
		fprintf(stdout, "socket(%s) kTLS enabled\n", sock_to_string(s));
	return 0;
}
static int sockmap_init_sockets(int verbose)
{
	int i, err, one = 1;
	struct sockaddr_in addr;
	int *fds[4] = {&s1, &s2, &c1, &c2};

	s1 = s2 = p1 = p2 = c1 = c2 = 0;

	/* Init sockets */
	for (i = 0; i < 4; i++) {
		*fds[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (*fds[i] < 0) {
			perror("socket s1 failed()");
			return errno;
		}
	}

	/* Allow reuse */
	for (i = 0; i < 2; i++) {
		err = setsockopt(*fds[i], SOL_SOCKET, SO_REUSEADDR,
				 (char *)&one, sizeof(one));
		if (err) {
			perror("setsockopt failed()");
			return errno;
		}
	}

	/* Non-blocking sockets */
	for (i = 0; i < 2; i++) {
		err = ioctl(*fds[i], FIONBIO, (char *)&one);
		if (err < 0) {
			perror("ioctl s1 failed()");
			return errno;
		}
	}

	/* Bind server sockets */
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	addr.sin_port = htons(S1_PORT);
	err = bind(s1, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0) {
		perror("bind s1 failed()");
		return errno;
	}

	addr.sin_port = htons(S2_PORT);
	err = bind(s2, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0) {
		perror("bind s2 failed()");
		return errno;
	}

	/* Listen server sockets */
	addr.sin_port = htons(S1_PORT);
	err = listen(s1, 32);
	if (err < 0) {
		perror("listen s1 failed()");
		return errno;
	}

	addr.sin_port = htons(S2_PORT);
	err = listen(s2, 32);
	if (err < 0) {
		perror("listen s1 failed()");
		return errno;
	}

	/* Initiate Connect */
	addr.sin_port = htons(S1_PORT);
	err = connect(c1, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0 && errno != EINPROGRESS) {
		perror("connect c1 failed()");
		return errno;
	}

	addr.sin_port = htons(S2_PORT);
	err = connect(c2, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0 && errno != EINPROGRESS) {
		perror("connect c2 failed()");
		return errno;
	} else if (err < 0) {
		err = 0;
	}

	/* Accept Connecrtions */
	p1 = accept(s1, NULL, NULL);
	if (p1 < 0) {
		perror("accept s1 failed()");
		return errno;
	}

	p2 = accept(s2, NULL, NULL);
	if (p2 < 0) {
		perror("accept s1 failed()");
		return errno;
	}

	if (verbose > 1) {
		printf("connected sockets: c1 <-> p1, c2 <-> p2\n");
		printf("cgroups binding: c1(%i) <-> s1(%i) - - - c2(%i) <-> s2(%i)\n",
			c1, s1, c2, s2);
	}
	return 0;
}

struct msg_stats {
	size_t bytes_sent;
	size_t bytes_recvd;
	struct timespec start;
	struct timespec end;
};

static int msg_loop_sendpage(int fd, int iov_length, int cnt,
			     struct msg_stats *s,
			     struct sockmap_options *opt)
{
	bool drop = opt->drop_expected;
	unsigned char k = 0;
	FILE *file;
	int i, fp;

	file = tmpfile();
	if (!file) {
		perror("create file for sendpage");
		return 1;
	}
	for (i = 0; i < iov_length * cnt; i++, k++)
		fwrite(&k, sizeof(char), 1, file);
	fflush(file);
	fseek(file, 0, SEEK_SET);

	fp = fileno(file);

	clock_gettime(CLOCK_MONOTONIC, &s->start);
	for (i = 0; i < cnt; i++) {
		int sent;

		errno = 0;
		sent = sendfile(fd, fp, NULL, iov_length);

		if (!drop && sent < 0) {
			perror("sendpage loop error");
			fclose(file);
			return sent;
		} else if (drop && sent >= 0) {
			printf("sendpage loop error expected: %i errno %i\n",
			       sent, errno);
			fclose(file);
			return -EIO;
		}

		if (sent > 0)
			s->bytes_sent += sent;
	}
	clock_gettime(CLOCK_MONOTONIC, &s->end);
	fclose(file);
	return 0;
}

static void msg_free_iov(struct msghdr *msg)
{
	int i;

	for (i = 0; i < msg->msg_iovlen; i++)
		free(msg->msg_iov[i].iov_base);
	free(msg->msg_iov);
	msg->msg_iov = NULL;
	msg->msg_iovlen = 0;
}

static int msg_alloc_iov(struct msghdr *msg,
			 int iov_count, int iov_length,
			 bool data, bool xmit)
{
	unsigned char k = 0;
	struct iovec *iov;
	int i;

	iov = calloc(iov_count, sizeof(struct iovec));
	if (!iov)
		return errno;

	for (i = 0; i < iov_count; i++) {
		unsigned char *d = calloc(iov_length, sizeof(char));

		if (!d) {
			fprintf(stderr, "iov_count %i/%i OOM\n", i, iov_count);
			goto unwind_iov;
		}
		iov[i].iov_base = d;
		iov[i].iov_len = iov_length;

		if (data && xmit) {
			int j;

			for (j = 0; j < iov_length; j++)
				d[j] = k++;
		}
	}

	msg->msg_iov = iov;
	msg->msg_iovlen = iov_count;

	return 0;
unwind_iov:
	for (i--; i >= 0 ; i--)
		free(msg->msg_iov[i].iov_base);
	return -ENOMEM;
}

static int msg_verify_data(struct msghdr *msg, int size, int chunk_sz)
{
	int i, j = 0, bytes_cnt = 0;
	unsigned char k = 0;

	for (i = 0; i < msg->msg_iovlen; i++) {
		unsigned char *d = msg->msg_iov[i].iov_base;

		/* Special case test for skb ingress + ktls */
		if (i == 0 && txmsg_ktls_skb) {
			if (msg->msg_iov[i].iov_len < 4)
				return -EIO;
			if (memcmp(d, "PASS", 4) != 0) {
				fprintf(stderr,
					"detected skb data error with skb ingress update @iov[%i]:%i \"%02x %02x %02x %02x\" != \"PASS\"\n",
					i, 0, d[0], d[1], d[2], d[3]);
				return -EIO;
			}
			j = 4; /* advance index past PASS header */
		}

		for (; j < msg->msg_iov[i].iov_len && size; j++) {
			if (d[j] != k++) {
				fprintf(stderr,
					"detected data corruption @iov[%i]:%i %02x != %02x, %02x ?= %02x\n",
					i, j, d[j], k - 1, d[j+1], k);
				return -EIO;
			}
			bytes_cnt++;
			if (bytes_cnt == chunk_sz) {
				k = 0;
				bytes_cnt = 0;
			}
			size--;
		}
	}
	return 0;
}

static int msg_loop(int fd, int iov_count, int iov_length, int cnt,
		    struct msg_stats *s, bool tx,
		    struct sockmap_options *opt)
{
	struct msghdr msg = {0}, msg_peek = {0};
	int err, i, flags = MSG_NOSIGNAL;
	bool drop = opt->drop_expected;
	bool data = opt->data_test;

	err = msg_alloc_iov(&msg, iov_count, iov_length, data, tx);
	if (err)
		goto out_errno;
	if (peek_flag) {
		err = msg_alloc_iov(&msg_peek, iov_count, iov_length, data, tx);
		if (err)
			goto out_errno;
	}

	if (tx) {
		clock_gettime(CLOCK_MONOTONIC, &s->start);
		for (i = 0; i < cnt; i++) {
			int sent;

			errno = 0;
			sent = sendmsg(fd, &msg, flags);

			if (!drop && sent < 0) {
				perror("sendmsg loop error");
				goto out_errno;
			} else if (drop && sent >= 0) {
				fprintf(stderr,
					"sendmsg loop error expected: %i errno %i\n",
					sent, errno);
				errno = -EIO;
				goto out_errno;
			}
			if (sent > 0)
				s->bytes_sent += sent;
		}
		clock_gettime(CLOCK_MONOTONIC, &s->end);
	} else {
		int slct, recvp = 0, recv, max_fd = fd;
		float total_bytes, txmsg_pop_total;
		int fd_flags = O_NONBLOCK;
		struct timeval timeout;
		fd_set w;

		fcntl(fd, fd_flags);
		/* Account for pop bytes noting each iteration of apply will
		 * call msg_pop_data helper so we need to account for this
		 * by calculating the number of apply iterations. Note user
		 * of the tool can create cases where no data is sent by
		 * manipulating pop/push/pull/etc. For example txmsg_apply 1
		 * with txmsg_pop 1 will try to apply 1B at a time but each
		 * iteration will then pop 1B so no data will ever be sent.
		 * This is really only useful for testing edge cases in code
		 * paths.
		 */
		total_bytes = (float)iov_count * (float)iov_length * (float)cnt;
		if (txmsg_apply)
			txmsg_pop_total = txmsg_pop * (total_bytes / txmsg_apply);
		else
			txmsg_pop_total = txmsg_pop * cnt;
		total_bytes -= txmsg_pop_total;
		err = clock_gettime(CLOCK_MONOTONIC, &s->start);
		if (err < 0)
			perror("recv start time");
		while (s->bytes_recvd < total_bytes) {
			if (txmsg_cork) {
				timeout.tv_sec = 0;
				timeout.tv_usec = 300000;
			} else {
				timeout.tv_sec = 3;
				timeout.tv_usec = 0;
			}

			/* FD sets */
			FD_ZERO(&w);
			FD_SET(fd, &w);

			slct = select(max_fd + 1, &w, NULL, NULL, &timeout);
			if (slct == -1) {
				perror("select()");
				clock_gettime(CLOCK_MONOTONIC, &s->end);
				goto out_errno;
			} else if (!slct) {
				if (opt->verbose)
					fprintf(stderr, "unexpected timeout: recved %zu/%f pop_total %f\n", s->bytes_recvd, total_bytes, txmsg_pop_total);
				errno = -EIO;
				clock_gettime(CLOCK_MONOTONIC, &s->end);
				goto out_errno;
			}

			errno = 0;
			if (peek_flag) {
				flags |= MSG_PEEK;
				recvp = recvmsg(fd, &msg_peek, flags);
				if (recvp < 0) {
					if (errno != EWOULDBLOCK) {
						clock_gettime(CLOCK_MONOTONIC, &s->end);
						goto out_errno;
					}
				}
				flags = 0;
			}

			recv = recvmsg(fd, &msg, flags);
			if (recv < 0) {
				if (errno != EWOULDBLOCK) {
					clock_gettime(CLOCK_MONOTONIC, &s->end);
					perror("recv failed()");
					goto out_errno;
				}
			}

			s->bytes_recvd += recv;

			if (data) {
				int chunk_sz = opt->sendpage ?
						iov_length * cnt :
						iov_length * iov_count;

				errno = msg_verify_data(&msg, recv, chunk_sz);
				if (errno) {
					perror("data verify msg failed");
					goto out_errno;
				}
				if (recvp) {
					errno = msg_verify_data(&msg_peek,
								recvp,
								chunk_sz);
					if (errno) {
						perror("data verify msg_peek failed");
						goto out_errno;
					}
				}
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &s->end);
	}

	msg_free_iov(&msg);
	msg_free_iov(&msg_peek);
	return err;
out_errno:
	msg_free_iov(&msg);
	msg_free_iov(&msg_peek);
	return errno;
}

static float giga = 1000000000;

static inline float sentBps(struct msg_stats s)
{
	return s.bytes_sent / (s.end.tv_sec - s.start.tv_sec);
}

static inline float recvdBps(struct msg_stats s)
{
	return s.bytes_recvd / (s.end.tv_sec - s.start.tv_sec);
}

static int sendmsg_test(struct sockmap_options *opt)
{
	float sent_Bps = 0, recvd_Bps = 0;
	int rx_fd, txpid, rxpid, err = 0;
	struct msg_stats s = {0};
	int iov_count = opt->iov_count;
	int iov_buf = opt->iov_length;
	int rx_status, tx_status;
	int cnt = opt->rate;

	errno = 0;

	if (opt->base)
		rx_fd = p1;
	else
		rx_fd = p2;

	if (ktls) {
		/* Redirecting into non-TLS socket which sends into a TLS
		 * socket is not a valid test. So in this case lets not
		 * enable kTLS but still run the test.
		 */
		if (!txmsg_redir || (txmsg_redir && txmsg_ingress)) {
			err = sockmap_init_ktls(opt->verbose, rx_fd);
			if (err)
				return err;
		}
		err = sockmap_init_ktls(opt->verbose, c1);
		if (err)
			return err;
	}

	rxpid = fork();
	if (rxpid == 0) {
		iov_buf -= (txmsg_pop - txmsg_start_pop + 1);
		if (opt->drop_expected || txmsg_ktls_skb_drop)
			_exit(0);

		if (!iov_buf) /* zero bytes sent case */
			_exit(0);

		if (opt->sendpage)
			iov_count = 1;
		err = msg_loop(rx_fd, iov_count, iov_buf,
			       cnt, &s, false, opt);
		if (opt->verbose > 1)
			fprintf(stderr,
				"msg_loop_rx: iov_count %i iov_buf %i cnt %i err %i\n",
				iov_count, iov_buf, cnt, err);
		if (s.end.tv_sec - s.start.tv_sec) {
			sent_Bps = sentBps(s);
			recvd_Bps = recvdBps(s);
		}
		if (opt->verbose > 1)
			fprintf(stdout,
				"rx_sendmsg: TX: %zuB %fB/s %fGB/s RX: %zuB %fB/s %fGB/s %s\n",
				s.bytes_sent, sent_Bps, sent_Bps/giga,
				s.bytes_recvd, recvd_Bps, recvd_Bps/giga,
				peek_flag ? "(peek_msg)" : "");
		if (err && txmsg_cork)
			err = 0;
		exit(err ? 1 : 0);
	} else if (rxpid == -1) {
		perror("msg_loop_rx");
		return errno;
	}

	txpid = fork();
	if (txpid == 0) {
		if (opt->sendpage)
			err = msg_loop_sendpage(c1, iov_buf, cnt, &s, opt);
		else
			err = msg_loop(c1, iov_count, iov_buf,
				       cnt, &s, true, opt);

		if (err)
			fprintf(stderr,
				"msg_loop_tx: iov_count %i iov_buf %i cnt %i err %i\n",
				iov_count, iov_buf, cnt, err);
		if (s.end.tv_sec - s.start.tv_sec) {
			sent_Bps = sentBps(s);
			recvd_Bps = recvdBps(s);
		}
		if (opt->verbose > 1)
			fprintf(stdout,
				"tx_sendmsg: TX: %zuB %fB/s %f GB/s RX: %zuB %fB/s %fGB/s\n",
				s.bytes_sent, sent_Bps, sent_Bps/giga,
				s.bytes_recvd, recvd_Bps, recvd_Bps/giga);
		exit(err ? 1 : 0);
	} else if (txpid == -1) {
		perror("msg_loop_tx");
		return errno;
	}

	assert(waitpid(rxpid, &rx_status, 0) == rxpid);
	assert(waitpid(txpid, &tx_status, 0) == txpid);
	if (WIFEXITED(rx_status)) {
		err = WEXITSTATUS(rx_status);
		if (err) {
			fprintf(stderr, "rx thread exited with err %d.\n", err);
			goto out;
		}
	}
	if (WIFEXITED(tx_status)) {
		err = WEXITSTATUS(tx_status);
		if (err)
			fprintf(stderr, "tx thread exited with err %d.\n", err);
	}
out:
	return err;
}

static int forever_ping_pong(int rate, struct sockmap_options *opt)
{
	struct timeval timeout;
	char buf[1024] = {0};
	int sc;

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	/* Ping/Pong data from client to server */
	sc = send(c1, buf, sizeof(buf), 0);
	if (sc < 0) {
		perror("send failed()");
		return sc;
	}

	do {
		int s, rc, i, max_fd = p2;
		fd_set w;

		/* FD sets */
		FD_ZERO(&w);
		FD_SET(c1, &w);
		FD_SET(c2, &w);
		FD_SET(p1, &w);
		FD_SET(p2, &w);

		s = select(max_fd + 1, &w, NULL, NULL, &timeout);
		if (s == -1) {
			perror("select()");
			break;
		} else if (!s) {
			fprintf(stderr, "unexpected timeout\n");
			break;
		}

		for (i = 0; i <= max_fd && s > 0; ++i) {
			if (!FD_ISSET(i, &w))
				continue;

			s--;

			rc = recv(i, buf, sizeof(buf), 0);
			if (rc < 0) {
				if (errno != EWOULDBLOCK) {
					perror("recv failed()");
					return rc;
				}
			}

			if (rc == 0) {
				close(i);
				break;
			}

			sc = send(i, buf, rc, 0);
			if (sc < 0) {
				perror("send failed()");
				return sc;
			}
		}

		if (rate)
			sleep(rate);

		if (opt->verbose) {
			printf(".");
			fflush(stdout);

		}
	} while (running);

	return 0;
}

enum {
	SELFTESTS,
	PING_PONG,
	SENDMSG,
	BASE,
	BASE_SENDPAGE,
	SENDPAGE,
};

static int run_options(struct sockmap_options *options, int cg_fd,  int test)
{
	int i, key, next_key, err, tx_prog_fd = -1, zero = 0;

	/* If base test skip BPF setup */
	if (test == BASE || test == BASE_SENDPAGE)
		goto run;

	/* Attach programs to sockmap */
	if (!txmsg_omit_skb_parser) {
		err = bpf_prog_attach(prog_fd[0], map_fd[0],
				      BPF_SK_SKB_STREAM_PARSER, 0);
		if (err) {
			fprintf(stderr,
				"ERROR: bpf_prog_attach (sockmap %i->%i): %d (%s)\n",
				prog_fd[0], map_fd[0], err, strerror(errno));
			return err;
		}
	}

	err = bpf_prog_attach(prog_fd[1], map_fd[0],
				BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err) {
		fprintf(stderr, "ERROR: bpf_prog_attach (sockmap): %d (%s)\n",
			err, strerror(errno));
		return err;
	}

	/* Attach programs to TLS sockmap */
	if (txmsg_ktls_skb) {
		if (!txmsg_omit_skb_parser) {
			err = bpf_prog_attach(prog_fd[0], map_fd[8],
					      BPF_SK_SKB_STREAM_PARSER, 0);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_prog_attach (TLS sockmap %i->%i): %d (%s)\n",
					prog_fd[0], map_fd[8], err, strerror(errno));
				return err;
			}
		}

		err = bpf_prog_attach(prog_fd[2], map_fd[8],
				      BPF_SK_SKB_STREAM_VERDICT, 0);
		if (err) {
			fprintf(stderr, "ERROR: bpf_prog_attach (TLS sockmap): %d (%s)\n",
				err, strerror(errno));
			return err;
		}
	}

	/* Attach to cgroups */
	err = bpf_prog_attach(prog_fd[3], cg_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (err) {
		fprintf(stderr, "ERROR: bpf_prog_attach (groups): %d (%s)\n",
			err, strerror(errno));
		return err;
	}

run:
	err = sockmap_init_sockets(options->verbose);
	if (err) {
		fprintf(stderr, "ERROR: test socket failed: %d\n", err);
		goto out;
	}

	/* Attach txmsg program to sockmap */
	if (txmsg_pass)
		tx_prog_fd = prog_fd[4];
	else if (txmsg_redir)
		tx_prog_fd = prog_fd[5];
	else if (txmsg_apply)
		tx_prog_fd = prog_fd[6];
	else if (txmsg_cork)
		tx_prog_fd = prog_fd[7];
	else if (txmsg_drop)
		tx_prog_fd = prog_fd[8];
	else
		tx_prog_fd = 0;

	if (tx_prog_fd) {
		int redir_fd, i = 0;

		err = bpf_prog_attach(tx_prog_fd,
				      map_fd[1], BPF_SK_MSG_VERDICT, 0);
		if (err) {
			fprintf(stderr,
				"ERROR: bpf_prog_attach (txmsg): %d (%s)\n",
				err, strerror(errno));
			goto out;
		}

		err = bpf_map_update_elem(map_fd[1], &i, &c1, BPF_ANY);
		if (err) {
			fprintf(stderr,
				"ERROR: bpf_map_update_elem (txmsg):  %d (%s\n",
				err, strerror(errno));
			goto out;
		}

		if (txmsg_redir)
			redir_fd = c2;
		else
			redir_fd = c1;

		err = bpf_map_update_elem(map_fd[2], &i, &redir_fd, BPF_ANY);
		if (err) {
			fprintf(stderr,
				"ERROR: bpf_map_update_elem (txmsg):  %d (%s\n",
				err, strerror(errno));
			goto out;
		}

		if (txmsg_apply) {
			err = bpf_map_update_elem(map_fd[3],
						  &i, &txmsg_apply, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (apply_bytes):  %d (%s\n",
					err, strerror(errno));
				goto out;
			}
		}

		if (txmsg_cork) {
			err = bpf_map_update_elem(map_fd[4],
						  &i, &txmsg_cork, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (cork_bytes):  %d (%s\n",
					err, strerror(errno));
				goto out;
			}
		}

		if (txmsg_start) {
			err = bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_start, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (txmsg_start):  %d (%s)\n",
					err, strerror(errno));
				goto out;
			}
		}

		if (txmsg_end) {
			i = 1;
			err = bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_end, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (txmsg_end):  %d (%s)\n",
					err, strerror(errno));
				goto out;
			}
		}

		if (txmsg_start_push) {
			i = 2;
			err = bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_start_push, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (txmsg_start_push):  %d (%s)\n",
					err, strerror(errno));
				goto out;
			}
		}

		if (txmsg_end_push) {
			i = 3;
			err = bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_end_push, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem %i@%i (txmsg_end_push):  %d (%s)\n",
					txmsg_end_push, i, err, strerror(errno));
				goto out;
			}
		}

		if (txmsg_start_pop) {
			i = 4;
			err = bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_start_pop, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem %i@%i (txmsg_start_pop):  %d (%s)\n",
					txmsg_start_pop, i, err, strerror(errno));
				goto out;
			}
		} else {
			i = 4;
			bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_start_pop, BPF_ANY);
		}

		if (txmsg_pop) {
			i = 5;
			err = bpf_map_update_elem(map_fd[5],
						  &i, &txmsg_pop, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem %i@%i (txmsg_pop):  %d (%s)\n",
					txmsg_pop, i, err, strerror(errno));
				goto out;
			}
		} else {
			i = 5;
			bpf_map_update_elem(map_fd[5],
					    &i, &txmsg_pop, BPF_ANY);

		}

		if (txmsg_ingress) {
			int in = BPF_F_INGRESS;

			i = 0;
			err = bpf_map_update_elem(map_fd[6], &i, &in, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (txmsg_ingress): %d (%s)\n",
					err, strerror(errno));
			}
			i = 1;
			err = bpf_map_update_elem(map_fd[1], &i, &p1, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (p1 txmsg): %d (%s)\n",
					err, strerror(errno));
			}
			err = bpf_map_update_elem(map_fd[2], &i, &p1, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (p1 redir): %d (%s)\n",
					err, strerror(errno));
			}

			i = 2;
			err = bpf_map_update_elem(map_fd[2], &i, &p2, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (p2 txmsg): %d (%s)\n",
					err, strerror(errno));
			}
		}

		if (txmsg_ktls_skb) {
			int ingress = BPF_F_INGRESS;

			i = 0;
			err = bpf_map_update_elem(map_fd[8], &i, &p2, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (c1 sockmap): %d (%s)\n",
					err, strerror(errno));
			}

			if (txmsg_ktls_skb_redir) {
				i = 1;
				err = bpf_map_update_elem(map_fd[7],
							  &i, &ingress, BPF_ANY);
				if (err) {
					fprintf(stderr,
						"ERROR: bpf_map_update_elem (txmsg_ingress): %d (%s)\n",
						err, strerror(errno));
				}
			}

			if (txmsg_ktls_skb_drop) {
				i = 1;
				err = bpf_map_update_elem(map_fd[7], &i, &i, BPF_ANY);
			}
		}

		if (txmsg_redir_skb) {
			int skb_fd = (test == SENDMSG || test == SENDPAGE) ?
					p2 : p1;
			int ingress = BPF_F_INGRESS;

			i = 0;
			err = bpf_map_update_elem(map_fd[7],
						  &i, &ingress, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (txmsg_ingress): %d (%s)\n",
					err, strerror(errno));
			}

			i = 3;
			err = bpf_map_update_elem(map_fd[0], &i, &skb_fd, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (c1 sockmap): %d (%s)\n",
					err, strerror(errno));
			}
		}
	}

	if (skb_use_parser) {
		i = 2;
		err = bpf_map_update_elem(map_fd[7], &i, &skb_use_parser, BPF_ANY);
	}

	if (txmsg_drop)
		options->drop_expected = true;

	if (test == PING_PONG)
		err = forever_ping_pong(options->rate, options);
	else if (test == SENDMSG) {
		options->base = false;
		options->sendpage = false;
		err = sendmsg_test(options);
	} else if (test == SENDPAGE) {
		options->base = false;
		options->sendpage = true;
		err = sendmsg_test(options);
	} else if (test == BASE) {
		options->base = true;
		options->sendpage = false;
		err = sendmsg_test(options);
	} else if (test == BASE_SENDPAGE) {
		options->base = true;
		options->sendpage = true;
		err = sendmsg_test(options);
	} else
		fprintf(stderr, "unknown test\n");
out:
	/* Detatch and zero all the maps */
	bpf_prog_detach2(prog_fd[3], cg_fd, BPF_CGROUP_SOCK_OPS);
	bpf_prog_detach2(prog_fd[0], map_fd[0], BPF_SK_SKB_STREAM_PARSER);
	bpf_prog_detach2(prog_fd[1], map_fd[0], BPF_SK_SKB_STREAM_VERDICT);
	bpf_prog_detach2(prog_fd[0], map_fd[8], BPF_SK_SKB_STREAM_PARSER);
	bpf_prog_detach2(prog_fd[2], map_fd[8], BPF_SK_SKB_STREAM_VERDICT);

	if (tx_prog_fd >= 0)
		bpf_prog_detach2(tx_prog_fd, map_fd[1], BPF_SK_MSG_VERDICT);

	for (i = 0; i < 8; i++) {
		key = next_key = 0;
		bpf_map_update_elem(map_fd[i], &key, &zero, BPF_ANY);
		while (bpf_map_get_next_key(map_fd[i], &key, &next_key) == 0) {
			bpf_map_update_elem(map_fd[i], &key, &zero, BPF_ANY);
			key = next_key;
		}
	}

	close(s1);
	close(s2);
	close(p1);
	close(p2);
	close(c1);
	close(c2);
	return err;
}

static char *test_to_str(int test)
{
	switch (test) {
	case SENDMSG:
		return "sendmsg";
	case SENDPAGE:
		return "sendpage";
	}
	return "unknown";
}

static void append_str(char *dst, const char *src, size_t dst_cap)
{
	size_t avail = dst_cap - strlen(dst);

	if (avail <= 1) /* just zero byte could be written */
		return;

	strncat(dst, src, avail - 1); /* strncat() adds + 1 for zero byte */
}

#define OPTSTRING 60
static void test_options(char *options)
{
	char tstr[OPTSTRING];

	memset(options, 0, OPTSTRING);

	if (txmsg_pass)
		append_str(options, "pass,", OPTSTRING);
	if (txmsg_redir)
		append_str(options, "redir,", OPTSTRING);
	if (txmsg_drop)
		append_str(options, "drop,", OPTSTRING);
	if (txmsg_apply) {
		snprintf(tstr, OPTSTRING, "apply %d,", txmsg_apply);
		append_str(options, tstr, OPTSTRING);
	}
	if (txmsg_cork) {
		snprintf(tstr, OPTSTRING, "cork %d,", txmsg_cork);
		append_str(options, tstr, OPTSTRING);
	}
	if (txmsg_start) {
		snprintf(tstr, OPTSTRING, "start %d,", txmsg_start);
		append_str(options, tstr, OPTSTRING);
	}
	if (txmsg_end) {
		snprintf(tstr, OPTSTRING, "end %d,", txmsg_end);
		append_str(options, tstr, OPTSTRING);
	}
	if (txmsg_start_pop) {
		snprintf(tstr, OPTSTRING, "pop (%d,%d),",
			 txmsg_start_pop, txmsg_start_pop + txmsg_pop);
		append_str(options, tstr, OPTSTRING);
	}
	if (txmsg_ingress)
		append_str(options, "ingress,", OPTSTRING);
	if (txmsg_redir_skb)
		append_str(options, "redir_skb,", OPTSTRING);
	if (txmsg_ktls_skb)
		append_str(options, "ktls_skb,", OPTSTRING);
	if (ktls)
		append_str(options, "ktls,", OPTSTRING);
	if (peek_flag)
		append_str(options, "peek,", OPTSTRING);
}

static int __test_exec(int cgrp, int test, struct sockmap_options *opt)
{
	char *options = calloc(OPTSTRING, sizeof(char));
	int err;

	if (test == SENDPAGE)
		opt->sendpage = true;
	else
		opt->sendpage = false;

	if (txmsg_drop)
		opt->drop_expected = true;
	else
		opt->drop_expected = false;

	test_options(options);

	if (opt->verbose) {
		fprintf(stdout,
			" [TEST %i]: (%i, %i, %i, %s, %s): ",
			test_cnt, opt->rate, opt->iov_count, opt->iov_length,
			test_to_str(test), options);
		fflush(stdout);
	}
	err = run_options(opt, cgrp, test);
	if (opt->verbose)
		fprintf(stdout, " %s\n", !err ? "PASS" : "FAILED");
	test_cnt++;
	!err ? passed++ : failed++;
	free(options);
	return err;
}

static void test_exec(int cgrp, struct sockmap_options *opt)
{
	int type = strcmp(opt->map, BPF_SOCKMAP_FILENAME);
	int err;

	if (type == 0) {
		test_start();
		err = __test_exec(cgrp, SENDMSG, opt);
		if (err)
			test_fail();
	} else {
		test_start();
		err = __test_exec(cgrp, SENDPAGE, opt);
		if (err)
			test_fail();
	}
}

static void test_send_one(struct sockmap_options *opt, int cgrp)
{
	opt->iov_length = 1;
	opt->iov_count = 1;
	opt->rate = 1;
	test_exec(cgrp, opt);

	opt->iov_length = 1;
	opt->iov_count = 1024;
	opt->rate = 1;
	test_exec(cgrp, opt);

	opt->iov_length = 1024;
	opt->iov_count = 1;
	opt->rate = 1;
	test_exec(cgrp, opt);

}

static void test_send_many(struct sockmap_options *opt, int cgrp)
{
	opt->iov_length = 3;
	opt->iov_count = 1;
	opt->rate = 512;
	test_exec(cgrp, opt);

	opt->rate = 100;
	opt->iov_count = 1;
	opt->iov_length = 5;
	test_exec(cgrp, opt);
}

static void test_send_large(struct sockmap_options *opt, int cgrp)
{
	opt->iov_length = 256;
	opt->iov_count = 1024;
	opt->rate = 2;
	test_exec(cgrp, opt);
}

static void test_send(struct sockmap_options *opt, int cgrp)
{
	test_send_one(opt, cgrp);
	test_send_many(opt, cgrp);
	test_send_large(opt, cgrp);
	sched_yield();
}

static void test_txmsg_pass(int cgrp, struct sockmap_options *opt)
{
	/* Test small and large iov_count values with pass/redir/apply/cork */
	txmsg_pass = 1;
	test_send(opt, cgrp);
}

static void test_txmsg_redir(int cgrp, struct sockmap_options *opt)
{
	txmsg_redir = 1;
	test_send(opt, cgrp);
}

static void test_txmsg_drop(int cgrp, struct sockmap_options *opt)
{
	txmsg_drop = 1;
	test_send(opt, cgrp);
}

static void test_txmsg_ingress_redir(int cgrp, struct sockmap_options *opt)
{
	txmsg_pass = txmsg_drop = 0;
	txmsg_ingress = txmsg_redir = 1;
	test_send(opt, cgrp);
}

static void test_txmsg_skb(int cgrp, struct sockmap_options *opt)
{
	bool data = opt->data_test;
	int k = ktls;

	opt->data_test = true;
	ktls = 1;

	txmsg_pass = txmsg_drop = 0;
	txmsg_ingress = txmsg_redir = 0;
	txmsg_ktls_skb = 1;
	txmsg_pass = 1;

	/* Using data verification so ensure iov layout is
	 * expected from test receiver side. e.g. has enough
	 * bytes to write test code.
	 */
	opt->iov_length = 100;
	opt->iov_count = 1;
	opt->rate = 1;
	test_exec(cgrp, opt);

	txmsg_ktls_skb_drop = 1;
	test_exec(cgrp, opt);

	txmsg_ktls_skb_drop = 0;
	txmsg_ktls_skb_redir = 1;
	test_exec(cgrp, opt);
	txmsg_ktls_skb_redir = 0;

	/* Tests that omit skb_parser */
	txmsg_omit_skb_parser = 1;
	ktls = 0;
	txmsg_ktls_skb = 0;
	test_exec(cgrp, opt);

	txmsg_ktls_skb_drop = 1;
	test_exec(cgrp, opt);
	txmsg_ktls_skb_drop = 0;

	txmsg_ktls_skb_redir = 1;
	test_exec(cgrp, opt);

	ktls = 1;
	test_exec(cgrp, opt);
	txmsg_omit_skb_parser = 0;

	opt->data_test = data;
	ktls = k;
}

/* Test cork with hung data. This tests poor usage patterns where
 * cork can leave data on the ring if user program is buggy and
 * doesn't flush them somehow. They do take some time however
 * because they wait for a timeout. Test pass, redir and cork with
 * apply logic. Use cork size of 4097 with send_large to avoid
 * aligning cork size with send size.
 */
static void test_txmsg_cork_hangs(int cgrp, struct sockmap_options *opt)
{
	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_cork = 4097;
	txmsg_apply = 4097;
	test_send_large(opt, cgrp);

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 0;
	txmsg_cork = 4097;
	test_send_large(opt, cgrp);

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 4097;
	txmsg_cork = 4097;
	test_send_large(opt, cgrp);
}

static void test_txmsg_pull(int cgrp, struct sockmap_options *opt)
{
	/* Test basic start/end */
	txmsg_start = 1;
	txmsg_end = 2;
	test_send(opt, cgrp);

	/* Test >4k pull */
	txmsg_start = 4096;
	txmsg_end = 9182;
	test_send_large(opt, cgrp);

	/* Test pull + redirect */
	txmsg_redir = 0;
	txmsg_start = 1;
	txmsg_end = 2;
	test_send(opt, cgrp);

	/* Test pull + cork */
	txmsg_redir = 0;
	txmsg_cork = 512;
	txmsg_start = 1;
	txmsg_end = 2;
	test_send_many(opt, cgrp);

	/* Test pull + cork + redirect */
	txmsg_redir = 1;
	txmsg_cork = 512;
	txmsg_start = 1;
	txmsg_end = 2;
	test_send_many(opt, cgrp);
}

static void test_txmsg_pop(int cgrp, struct sockmap_options *opt)
{
	/* Test basic pop */
	txmsg_start_pop = 1;
	txmsg_pop = 2;
	test_send_many(opt, cgrp);

	/* Test pop with >4k */
	txmsg_start_pop = 4096;
	txmsg_pop = 4096;
	test_send_large(opt, cgrp);

	/* Test pop + redirect */
	txmsg_redir = 1;
	txmsg_start_pop = 1;
	txmsg_pop = 2;
	test_send_many(opt, cgrp);

	/* Test pop + cork */
	txmsg_redir = 0;
	txmsg_cork = 512;
	txmsg_start_pop = 1;
	txmsg_pop = 2;
	test_send_many(opt, cgrp);

	/* Test pop + redirect + cork */
	txmsg_redir = 1;
	txmsg_cork = 4;
	txmsg_start_pop = 1;
	txmsg_pop = 2;
	test_send_many(opt, cgrp);
}

static void test_txmsg_push(int cgrp, struct sockmap_options *opt)
{
	/* Test basic push */
	txmsg_start_push = 1;
	txmsg_end_push = 1;
	test_send(opt, cgrp);

	/* Test push 4kB >4k */
	txmsg_start_push = 4096;
	txmsg_end_push = 4096;
	test_send_large(opt, cgrp);

	/* Test push + redirect */
	txmsg_redir = 1;
	txmsg_start_push = 1;
	txmsg_end_push = 2;
	test_send_many(opt, cgrp);

	/* Test push + cork */
	txmsg_redir = 0;
	txmsg_cork = 512;
	txmsg_start_push = 1;
	txmsg_end_push = 2;
	test_send_many(opt, cgrp);
}

static void test_txmsg_push_pop(int cgrp, struct sockmap_options *opt)
{
	txmsg_start_push = 1;
	txmsg_end_push = 10;
	txmsg_start_pop = 5;
	txmsg_pop = 4;
	test_send_large(opt, cgrp);
}

static void test_txmsg_apply(int cgrp, struct sockmap_options *opt)
{
	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1;
	txmsg_cork = 0;
	test_send_one(opt, cgrp);

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 1;
	txmsg_cork = 0;
	test_send_one(opt, cgrp);

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1024;
	txmsg_cork = 0;
	test_send_large(opt, cgrp);

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 1024;
	txmsg_cork = 0;
	test_send_large(opt, cgrp);
}

static void test_txmsg_cork(int cgrp, struct sockmap_options *opt)
{
	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 0;
	txmsg_cork = 1;
	test_send(opt, cgrp);

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1;
	txmsg_cork = 1;
	test_send(opt, cgrp);
}

static void test_txmsg_ingress_parser(int cgrp, struct sockmap_options *opt)
{
	txmsg_pass = 1;
	skb_use_parser = 512;
	opt->iov_length = 256;
	opt->iov_count = 1;
	opt->rate = 2;
	test_exec(cgrp, opt);
}

char *map_names[] = {
	"sock_map",
	"sock_map_txmsg",
	"sock_map_redir",
	"sock_apply_bytes",
	"sock_cork_bytes",
	"sock_bytes",
	"sock_redir_flags",
	"sock_skb_opts",
	"tls_sock_map",
};

int prog_attach_type[] = {
	BPF_SK_SKB_STREAM_PARSER,
	BPF_SK_SKB_STREAM_VERDICT,
	BPF_SK_SKB_STREAM_VERDICT,
	BPF_CGROUP_SOCK_OPS,
	BPF_SK_MSG_VERDICT,
	BPF_SK_MSG_VERDICT,
	BPF_SK_MSG_VERDICT,
	BPF_SK_MSG_VERDICT,
	BPF_SK_MSG_VERDICT,
	BPF_SK_MSG_VERDICT,
	BPF_SK_MSG_VERDICT,
};

int prog_type[] = {
	BPF_PROG_TYPE_SK_SKB,
	BPF_PROG_TYPE_SK_SKB,
	BPF_PROG_TYPE_SK_SKB,
	BPF_PROG_TYPE_SOCK_OPS,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_SK_MSG,
};

static int populate_progs(char *bpf_file)
{
	struct bpf_program *prog;
	struct bpf_object *obj;
	int i = 0;
	long err;

	obj = bpf_object__open(bpf_file);
	err = libbpf_get_error(obj);
	if (err) {
		char err_buf[256];

		libbpf_strerror(err, err_buf, sizeof(err_buf));
		printf("Unable to load eBPF objects in file '%s' : %s\n",
		       bpf_file, err_buf);
		return -1;
	}

	bpf_object__for_each_program(prog, obj) {
		bpf_program__set_type(prog, prog_type[i]);
		bpf_program__set_expected_attach_type(prog,
						      prog_attach_type[i]);
		i++;
	}

	i = bpf_object__load(obj);
	i = 0;
	bpf_object__for_each_program(prog, obj) {
		prog_fd[i] = bpf_program__fd(prog);
		i++;
	}

	for (i = 0; i < sizeof(map_fd)/sizeof(int); i++) {
		maps[i] = bpf_object__find_map_by_name(obj, map_names[i]);
		map_fd[i] = bpf_map__fd(maps[i]);
		if (map_fd[i] < 0) {
			fprintf(stderr, "load_bpf_file: (%i) %s\n",
				map_fd[i], strerror(errno));
			return -1;
		}
	}

	return 0;
}

struct _test test[] = {
	{"txmsg test passthrough", test_txmsg_pass},
	{"txmsg test redirect", test_txmsg_redir},
	{"txmsg test drop", test_txmsg_drop},
	{"txmsg test ingress redirect", test_txmsg_ingress_redir},
	{"txmsg test skb", test_txmsg_skb},
	{"txmsg test apply", test_txmsg_apply},
	{"txmsg test cork", test_txmsg_cork},
	{"txmsg test hanging corks", test_txmsg_cork_hangs},
	{"txmsg test push_data", test_txmsg_push},
	{"txmsg test pull-data", test_txmsg_pull},
	{"txmsg test pop-data", test_txmsg_pop},
	{"txmsg test push/pop data", test_txmsg_push_pop},
	{"txmsg text ingress parser", test_txmsg_ingress_parser},
};

static int check_whitelist(struct _test *t, struct sockmap_options *opt)
{
	char *entry, *ptr;

	if (!opt->whitelist)
		return 0;
	ptr = strdup(opt->whitelist);
	if (!ptr)
		return -ENOMEM;
	entry = strtok(ptr, ",");
	while (entry) {
		if ((opt->prepend && strstr(opt->prepend, entry) != 0) ||
		    strstr(opt->map, entry) != 0 ||
		    strstr(t->title, entry) != 0)
			return 0;
		entry = strtok(NULL, ",");
	}
	return -EINVAL;
}

static int check_blacklist(struct _test *t, struct sockmap_options *opt)
{
	char *entry, *ptr;

	if (!opt->blacklist)
		return -EINVAL;
	ptr = strdup(opt->blacklist);
	if (!ptr)
		return -ENOMEM;
	entry = strtok(ptr, ",");
	while (entry) {
		if ((opt->prepend && strstr(opt->prepend, entry) != 0) ||
		    strstr(opt->map, entry) != 0 ||
		    strstr(t->title, entry) != 0)
			return 0;
		entry = strtok(NULL, ",");
	}
	return -EINVAL;
}

static int __test_selftests(int cg_fd, struct sockmap_options *opt)
{
	int i, err;

	err = populate_progs(opt->map);
	if (err < 0) {
		fprintf(stderr, "ERROR: (%i) load bpf failed\n", err);
		return err;
	}

	/* Tests basic commands and APIs */
	for (i = 0; i < sizeof(test)/sizeof(struct _test); i++) {
		struct _test t = test[i];

		if (check_whitelist(&t, opt) != 0)
			continue;
		if (check_blacklist(&t, opt) == 0)
			continue;

		test_start_subtest(&t, opt);
		t.tester(cg_fd, opt);
		test_end_subtest();
	}

	return err;
}

static void test_selftests_sockmap(int cg_fd, struct sockmap_options *opt)
{
	opt->map = BPF_SOCKMAP_FILENAME;
	__test_selftests(cg_fd, opt);
}

static void test_selftests_sockhash(int cg_fd, struct sockmap_options *opt)
{
	opt->map = BPF_SOCKHASH_FILENAME;
	__test_selftests(cg_fd, opt);
}

static void test_selftests_ktls(int cg_fd, struct sockmap_options *opt)
{
	opt->map = BPF_SOCKHASH_FILENAME;
	opt->prepend = "ktls";
	ktls = 1;
	__test_selftests(cg_fd, opt);
	ktls = 0;
}

static int test_selftest(int cg_fd, struct sockmap_options *opt)
{

	test_selftests_sockmap(cg_fd, opt);
	test_selftests_sockhash(cg_fd, opt);
	test_selftests_ktls(cg_fd, opt);
	test_print_results();
	return 0;
}

int main(int argc, char **argv)
{
	int iov_count = 1, length = 1024, rate = 1;
	struct sockmap_options options = {0};
	int opt, longindex, err, cg_fd = 0;
	char *bpf_file = BPF_SOCKMAP_FILENAME;
	int test = SELFTESTS;
	bool cg_created = 0;

	while ((opt = getopt_long(argc, argv, ":dhv:c:r:i:l:t:p:q:n:b:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 's':
			txmsg_start = atoi(optarg);
			break;
		case 'e':
			txmsg_end = atoi(optarg);
			break;
		case 'p':
			txmsg_start_push = atoi(optarg);
			break;
		case 'q':
			txmsg_end_push = atoi(optarg);
			break;
		case 'w':
			txmsg_start_pop = atoi(optarg);
			break;
		case 'x':
			txmsg_pop = atoi(optarg);
			break;
		case 'a':
			txmsg_apply = atoi(optarg);
			break;
		case 'k':
			txmsg_cork = atoi(optarg);
			break;
		case 'c':
			cg_fd = open(optarg, O_DIRECTORY, O_RDONLY);
			if (cg_fd < 0) {
				fprintf(stderr,
					"ERROR: (%i) open cg path failed: %s\n",
					cg_fd, optarg);
				return cg_fd;
			}
			break;
		case 'r':
			rate = atoi(optarg);
			break;
		case 'v':
			options.verbose = 1;
			if (optarg)
				options.verbose = atoi(optarg);
			break;
		case 'i':
			iov_count = atoi(optarg);
			break;
		case 'l':
			length = atoi(optarg);
			break;
		case 'd':
			options.data_test = true;
			break;
		case 't':
			if (strcmp(optarg, "ping") == 0) {
				test = PING_PONG;
			} else if (strcmp(optarg, "sendmsg") == 0) {
				test = SENDMSG;
			} else if (strcmp(optarg, "base") == 0) {
				test = BASE;
			} else if (strcmp(optarg, "base_sendpage") == 0) {
				test = BASE_SENDPAGE;
			} else if (strcmp(optarg, "sendpage") == 0) {
				test = SENDPAGE;
			} else {
				usage(argv);
				return -1;
			}
			break;
		case 'n':
			options.whitelist = strdup(optarg);
			if (!options.whitelist)
				return -ENOMEM;
			break;
		case 'b':
			options.blacklist = strdup(optarg);
			if (!options.blacklist)
				return -ENOMEM;
		case 0:
			break;
		case 'h':
		default:
			usage(argv);
			return -1;
		}
	}

	if (!cg_fd) {
		cg_fd = cgroup_setup_and_join(CG_PATH);
		if (cg_fd < 0)
			return cg_fd;
		cg_created = 1;
	}

	if (test == SELFTESTS) {
		err = test_selftest(cg_fd, &options);
		goto out;
	}

	err = populate_progs(bpf_file);
	if (err) {
		fprintf(stderr, "populate program: (%s) %s\n",
			bpf_file, strerror(errno));
		return 1;
	}
	running = 1;

	/* catch SIGINT */
	signal(SIGINT, running_handler);

	options.iov_count = iov_count;
	options.iov_length = length;
	options.rate = rate;

	err = run_options(&options, cg_fd, test);
out:
	if (options.whitelist)
		free(options.whitelist);
	if (options.blacklist)
		free(options.blacklist);
	if (cg_created)
		cleanup_cgroup_environment();
	close(cg_fd);
	return err;
}

void running_handler(int a)
{
	running = 0;
}
