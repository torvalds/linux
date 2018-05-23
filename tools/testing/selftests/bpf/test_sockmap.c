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
#include <sys/ioctl.h>
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

/* randomly selected ports for testing on lo */
#define S1_PORT 10000
#define S2_PORT 10001

#define BPF_SOCKMAP_FILENAME "test_sockmap_kern.o"
#define BPF_SOCKHASH_FILENAME "test_sockhash_kern.o"
#define CG_PATH "/sockmap"

/* global sockets */
int s1, s2, c1, c2, p1, p2;
int test_cnt;
int passed;
int failed;
int map_fd[8];
struct bpf_map *maps[8];
int prog_fd[11];

int txmsg_pass;
int txmsg_noisy;
int txmsg_redir;
int txmsg_redir_noisy;
int txmsg_drop;
int txmsg_apply;
int txmsg_cork;
int txmsg_start;
int txmsg_end;
int txmsg_ingress;
int txmsg_skb;

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"cgroup",	required_argument,	NULL, 'c' },
	{"rate",	required_argument,	NULL, 'r' },
	{"verbose",	no_argument,		NULL, 'v' },
	{"iov_count",	required_argument,	NULL, 'i' },
	{"length",	required_argument,	NULL, 'l' },
	{"test",	required_argument,	NULL, 't' },
	{"data_test",   no_argument,		NULL, 'd' },
	{"txmsg",		no_argument,	&txmsg_pass,  1  },
	{"txmsg_noisy",		no_argument,	&txmsg_noisy, 1  },
	{"txmsg_redir",		no_argument,	&txmsg_redir, 1  },
	{"txmsg_redir_noisy",	no_argument,	&txmsg_redir_noisy, 1},
	{"txmsg_drop",		no_argument,	&txmsg_drop, 1 },
	{"txmsg_apply",	required_argument,	NULL, 'a'},
	{"txmsg_cork",	required_argument,	NULL, 'k'},
	{"txmsg_start", required_argument,	NULL, 's'},
	{"txmsg_end",	required_argument,	NULL, 'e'},
	{"txmsg_ingress", no_argument,		&txmsg_ingress, 1 },
	{"txmsg_skb", no_argument,		&txmsg_skb, 1 },
	{0, 0, NULL, 0 }
};

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
		perror("bind s1 failed()\n");
		return errno;
	}

	addr.sin_port = htons(S2_PORT);
	err = bind(s2, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0) {
		perror("bind s2 failed()\n");
		return errno;
	}

	/* Listen server sockets */
	addr.sin_port = htons(S1_PORT);
	err = listen(s1, 32);
	if (err < 0) {
		perror("listen s1 failed()\n");
		return errno;
	}

	addr.sin_port = htons(S2_PORT);
	err = listen(s2, 32);
	if (err < 0) {
		perror("listen s1 failed()\n");
		return errno;
	}

	/* Initiate Connect */
	addr.sin_port = htons(S1_PORT);
	err = connect(c1, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0 && errno != EINPROGRESS) {
		perror("connect c1 failed()\n");
		return errno;
	}

	addr.sin_port = htons(S2_PORT);
	err = connect(c2, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0 && errno != EINPROGRESS) {
		perror("connect c2 failed()\n");
		return errno;
	} else if (err < 0) {
		err = 0;
	}

	/* Accept Connecrtions */
	p1 = accept(s1, NULL, NULL);
	if (p1 < 0) {
		perror("accept s1 failed()\n");
		return errno;
	}

	p2 = accept(s2, NULL, NULL);
	if (p2 < 0) {
		perror("accept s1 failed()\n");
		return errno;
	}

	if (verbose) {
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

struct sockmap_options {
	int verbose;
	bool base;
	bool sendpage;
	bool data_test;
	bool drop_expected;
	int iov_count;
	int iov_length;
	int rate;
};

static int msg_loop_sendpage(int fd, int iov_length, int cnt,
			     struct msg_stats *s,
			     struct sockmap_options *opt)
{
	bool drop = opt->drop_expected;
	unsigned char k = 0;
	FILE *file;
	int i, fp;

	file = fopen(".sendpage_tst.tmp", "w+");
	for (i = 0; i < iov_length * cnt; i++, k++)
		fwrite(&k, sizeof(char), 1, file);
	fflush(file);
	fseek(file, 0, SEEK_SET);
	fclose(file);

	fp = open(".sendpage_tst.tmp", O_RDONLY);
	clock_gettime(CLOCK_MONOTONIC, &s->start);
	for (i = 0; i < cnt; i++) {
		int sent = sendfile(fd, fp, NULL, iov_length);

		if (!drop && sent < 0) {
			perror("send loop error:");
			close(fp);
			return sent;
		} else if (drop && sent >= 0) {
			printf("sendpage loop error expected: %i\n", sent);
			close(fp);
			return -EIO;
		}

		if (sent > 0)
			s->bytes_sent += sent;
	}
	clock_gettime(CLOCK_MONOTONIC, &s->end);
	close(fp);
	return 0;
}

static int msg_loop(int fd, int iov_count, int iov_length, int cnt,
		    struct msg_stats *s, bool tx,
		    struct sockmap_options *opt)
{
	struct msghdr msg = {0};
	int err, i, flags = MSG_NOSIGNAL;
	struct iovec *iov;
	unsigned char k;
	bool data_test = opt->data_test;
	bool drop = opt->drop_expected;

	iov = calloc(iov_count, sizeof(struct iovec));
	if (!iov)
		return errno;

	k = 0;
	for (i = 0; i < iov_count; i++) {
		unsigned char *d = calloc(iov_length, sizeof(char));

		if (!d) {
			fprintf(stderr, "iov_count %i/%i OOM\n", i, iov_count);
			goto out_errno;
		}
		iov[i].iov_base = d;
		iov[i].iov_len = iov_length;

		if (data_test && tx) {
			int j;

			for (j = 0; j < iov_length; j++)
				d[j] = k++;
		}
	}

	msg.msg_iov = iov;
	msg.msg_iovlen = iov_count;
	k = 0;

	if (tx) {
		clock_gettime(CLOCK_MONOTONIC, &s->start);
		for (i = 0; i < cnt; i++) {
			int sent = sendmsg(fd, &msg, flags);

			if (!drop && sent < 0) {
				perror("send loop error:");
				goto out_errno;
			} else if (drop && sent >= 0) {
				printf("send loop error expected: %i\n", sent);
				errno = -EIO;
				goto out_errno;
			}
			if (sent > 0)
				s->bytes_sent += sent;
		}
		clock_gettime(CLOCK_MONOTONIC, &s->end);
	} else {
		int slct, recv, max_fd = fd;
		int fd_flags = O_NONBLOCK;
		struct timeval timeout;
		float total_bytes;
		fd_set w;

		fcntl(fd, fd_flags);
		total_bytes = (float)iov_count * (float)iov_length * (float)cnt;
		err = clock_gettime(CLOCK_MONOTONIC, &s->start);
		if (err < 0)
			perror("recv start time: ");
		while (s->bytes_recvd < total_bytes) {
			timeout.tv_sec = 0;
			timeout.tv_usec = 10;

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
					fprintf(stderr, "unexpected timeout\n");
				errno = -EIO;
				clock_gettime(CLOCK_MONOTONIC, &s->end);
				goto out_errno;
			}

			recv = recvmsg(fd, &msg, flags);
			if (recv < 0) {
				if (errno != EWOULDBLOCK) {
					clock_gettime(CLOCK_MONOTONIC, &s->end);
					perror("recv failed()\n");
					goto out_errno;
				}
			}

			s->bytes_recvd += recv;

			if (data_test) {
				int j;

				for (i = 0; i < msg.msg_iovlen; i++) {
					unsigned char *d = iov[i].iov_base;

					for (j = 0;
					     j < iov[i].iov_len && recv; j++) {
						if (d[j] != k++) {
							errno = -EIO;
							fprintf(stderr,
								"detected data corruption @iov[%i]:%i %02x != %02x, %02x ?= %02x\n",
								i, j, d[j], k - 1, d[j+1], k + 1);
							goto out_errno;
						}
						recv--;
					}
				}
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &s->end);
	}

	for (i = 0; i < iov_count; i++)
		free(iov[i].iov_base);
	free(iov);
	return 0;
out_errno:
	for (i = 0; i < iov_count; i++)
		free(iov[i].iov_base);
	free(iov);
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
	int cnt = opt->rate;
	int status;

	errno = 0;

	if (opt->base)
		rx_fd = p1;
	else
		rx_fd = p2;

	rxpid = fork();
	if (rxpid == 0) {
		if (opt->drop_expected)
			exit(1);

		if (opt->sendpage)
			iov_count = 1;
		err = msg_loop(rx_fd, iov_count, iov_buf,
			       cnt, &s, false, opt);
		if (err && opt->verbose)
			fprintf(stderr,
				"msg_loop_rx: iov_count %i iov_buf %i cnt %i err %i\n",
				iov_count, iov_buf, cnt, err);
		shutdown(p2, SHUT_RDWR);
		shutdown(p1, SHUT_RDWR);
		if (s.end.tv_sec - s.start.tv_sec) {
			sent_Bps = sentBps(s);
			recvd_Bps = recvdBps(s);
		}
		if (opt->verbose)
			fprintf(stdout,
				"rx_sendmsg: TX: %zuB %fB/s %fGB/s RX: %zuB %fB/s %fGB/s\n",
				s.bytes_sent, sent_Bps, sent_Bps/giga,
				s.bytes_recvd, recvd_Bps, recvd_Bps/giga);
		exit(1);
	} else if (rxpid == -1) {
		perror("msg_loop_rx: ");
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
		shutdown(c1, SHUT_RDWR);
		if (s.end.tv_sec - s.start.tv_sec) {
			sent_Bps = sentBps(s);
			recvd_Bps = recvdBps(s);
		}
		if (opt->verbose)
			fprintf(stdout,
				"tx_sendmsg: TX: %zuB %fB/s %f GB/s RX: %zuB %fB/s %fGB/s\n",
				s.bytes_sent, sent_Bps, sent_Bps/giga,
				s.bytes_recvd, recvd_Bps, recvd_Bps/giga);
		exit(1);
	} else if (txpid == -1) {
		perror("msg_loop_tx: ");
		return errno;
	}

	assert(waitpid(rxpid, &status, 0) == rxpid);
	assert(waitpid(txpid, &status, 0) == txpid);
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
		perror("send failed()\n");
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
					perror("recv failed()\n");
					return rc;
				}
			}

			if (rc == 0) {
				close(i);
				break;
			}

			sc = send(i, buf, rc, 0);
			if (sc < 0) {
				perror("send failed()\n");
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
	err = bpf_prog_attach(prog_fd[0], map_fd[0],
				BPF_SK_SKB_STREAM_PARSER, 0);
	if (err) {
		fprintf(stderr,
			"ERROR: bpf_prog_attach (sockmap %i->%i): %d (%s)\n",
			prog_fd[0], map_fd[0], err, strerror(errno));
		return err;
	}

	err = bpf_prog_attach(prog_fd[1], map_fd[0],
				BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err) {
		fprintf(stderr, "ERROR: bpf_prog_attach (sockmap): %d (%s)\n",
			err, strerror(errno));
		return err;
	}

	/* Attach to cgroups */
	err = bpf_prog_attach(prog_fd[2], cg_fd, BPF_CGROUP_SOCK_OPS, 0);
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
		tx_prog_fd = prog_fd[3];
	else if (txmsg_noisy)
		tx_prog_fd = prog_fd[4];
	else if (txmsg_redir)
		tx_prog_fd = prog_fd[5];
	else if (txmsg_redir_noisy)
		tx_prog_fd = prog_fd[6];
	else if (txmsg_drop)
		tx_prog_fd = prog_fd[9];
	/* apply and cork must be last */
	else if (txmsg_apply)
		tx_prog_fd = prog_fd[7];
	else if (txmsg_cork)
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

		if (txmsg_redir || txmsg_redir_noisy)
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

		if (txmsg_skb) {
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
			err = bpf_map_update_elem(map_fd[0],
						  &i, &skb_fd, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"ERROR: bpf_map_update_elem (c1 sockmap): %d (%s)\n",
					err, strerror(errno));
			}
		}
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
	bpf_prog_detach2(prog_fd[2], cg_fd, BPF_CGROUP_SOCK_OPS);
	bpf_prog_detach2(prog_fd[0], map_fd[0], BPF_SK_SKB_STREAM_PARSER);
	bpf_prog_detach2(prog_fd[1], map_fd[0], BPF_SK_SKB_STREAM_VERDICT);
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

#define OPTSTRING 60
static void test_options(char *options)
{
	memset(options, 0, OPTSTRING);

	if (txmsg_pass)
		strncat(options, "pass,", OPTSTRING);
	if (txmsg_noisy)
		strncat(options, "pass_noisy,", OPTSTRING);
	if (txmsg_redir)
		strncat(options, "redir,", OPTSTRING);
	if (txmsg_redir_noisy)
		strncat(options, "redir_noisy,", OPTSTRING);
	if (txmsg_drop)
		strncat(options, "drop,", OPTSTRING);
	if (txmsg_apply)
		strncat(options, "apply,", OPTSTRING);
	if (txmsg_cork)
		strncat(options, "cork,", OPTSTRING);
	if (txmsg_start)
		strncat(options, "start,", OPTSTRING);
	if (txmsg_end)
		strncat(options, "end,", OPTSTRING);
	if (txmsg_ingress)
		strncat(options, "ingress,", OPTSTRING);
	if (txmsg_skb)
		strncat(options, "skb,", OPTSTRING);
}

static int __test_exec(int cgrp, int test, struct sockmap_options *opt)
{
	char *options = calloc(60, sizeof(char));
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

	fprintf(stdout,
		"[TEST %i]: (%i, %i, %i, %s, %s): ",
		test_cnt, opt->rate, opt->iov_count, opt->iov_length,
		test_to_str(test), options);
	fflush(stdout);
	err = run_options(opt, cgrp, test);
	fprintf(stdout, "%s\n", !err ? "PASS" : "FAILED");
	test_cnt++;
	!err ? passed++ : failed++;
	free(options);
	return err;
}

static int test_exec(int cgrp, struct sockmap_options *opt)
{
	int err = __test_exec(cgrp, SENDMSG, opt);

	if (err)
		goto out;

	err = __test_exec(cgrp, SENDPAGE, opt);
out:
	return err;
}

static int test_loop(int cgrp)
{
	struct sockmap_options opt;

	int err, i, l, r;

	opt.verbose = 0;
	opt.base = false;
	opt.sendpage = false;
	opt.data_test = false;
	opt.drop_expected = false;
	opt.iov_count = 0;
	opt.iov_length = 0;
	opt.rate = 0;

	r = 1;
	for (i = 1; i < 100; i += 33) {
		for (l = 1; l < 100; l += 33) {
			opt.rate = r;
			opt.iov_count = i;
			opt.iov_length = l;
			err = test_exec(cgrp, &opt);
			if (err)
				goto out;
		}
	}
	sched_yield();
out:
	return err;
}

static int test_txmsg(int cgrp)
{
	int err;

	txmsg_pass = txmsg_noisy = txmsg_redir_noisy = txmsg_drop = 0;
	txmsg_apply = txmsg_cork = 0;
	txmsg_ingress = txmsg_skb = 0;

	txmsg_pass = 1;
	err = test_loop(cgrp);
	txmsg_pass = 0;
	if (err)
		goto out;

	txmsg_redir = 1;
	err = test_loop(cgrp);
	txmsg_redir = 0;
	if (err)
		goto out;

	txmsg_drop = 1;
	err = test_loop(cgrp);
	txmsg_drop = 0;
	if (err)
		goto out;

	txmsg_redir = 1;
	txmsg_ingress = 1;
	err = test_loop(cgrp);
	txmsg_redir = 0;
	txmsg_ingress = 0;
	if (err)
		goto out;
out:
	txmsg_pass = 0;
	txmsg_redir = 0;
	txmsg_drop = 0;
	return err;
}

static int test_send(struct sockmap_options *opt, int cgrp)
{
	int err;

	opt->iov_length = 1;
	opt->iov_count = 1;
	opt->rate = 1;
	err = test_exec(cgrp, opt);
	if (err)
		goto out;

	opt->iov_length = 1;
	opt->iov_count = 1024;
	opt->rate = 1;
	err = test_exec(cgrp, opt);
	if (err)
		goto out;

	opt->iov_length = 1024;
	opt->iov_count = 1;
	opt->rate = 1;
	err = test_exec(cgrp, opt);
	if (err)
		goto out;

	opt->iov_length = 1;
	opt->iov_count = 1;
	opt->rate = 1024;
	err = test_exec(cgrp, opt);
	if (err)
		goto out;

	opt->iov_length = 256;
	opt->iov_count = 1024;
	opt->rate = 10;
	err = test_exec(cgrp, opt);
	if (err)
		goto out;

	opt->rate = 100;
	opt->iov_count = 1;
	opt->iov_length = 5;
	err = test_exec(cgrp, opt);
	if (err)
		goto out;
out:
	sched_yield();
	return err;
}

static int test_mixed(int cgrp)
{
	struct sockmap_options opt = {0};
	int err;

	txmsg_pass = txmsg_noisy = txmsg_redir_noisy = txmsg_drop = 0;
	txmsg_apply = txmsg_cork = 0;
	txmsg_start = txmsg_end = 0;
	/* Test small and large iov_count values with pass/redir/apply/cork */
	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1;
	txmsg_cork = 0;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 0;
	txmsg_cork = 1;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1;
	txmsg_cork = 1;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1024;
	txmsg_cork = 0;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 0;
	txmsg_cork = 1024;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_apply = 1024;
	txmsg_cork = 1024;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 1;
	txmsg_redir = 0;
	txmsg_cork = 4096;
	txmsg_apply = 4096;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 1;
	txmsg_cork = 0;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 0;
	txmsg_cork = 1;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 1024;
	txmsg_cork = 0;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 0;
	txmsg_cork = 1024;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_apply = 1024;
	txmsg_cork = 1024;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;

	txmsg_pass = 0;
	txmsg_redir = 1;
	txmsg_cork = 4096;
	txmsg_apply = 4096;
	err = test_send(&opt, cgrp);
	if (err)
		goto out;
out:
	return err;
}

static int test_start_end(int cgrp)
{
	struct sockmap_options opt = {0};
	int err, i;

	/* Test basic start/end with lots of iov_count and iov_lengths */
	txmsg_start = 1;
	txmsg_end = 2;
	err = test_txmsg(cgrp);
	if (err)
		goto out;

	/* Test start/end with cork */
	opt.rate = 16;
	opt.iov_count = 1;
	opt.iov_length = 100;
	txmsg_cork = 1600;

	for (i = 99; i <= 1600; i += 500) {
		txmsg_start = 0;
		txmsg_end = i;
		err = test_exec(cgrp, &opt);
		if (err)
			goto out;
	}

	/* Test start/end with cork but pull data in middle */
	for (i = 199; i <= 1600; i += 500) {
		txmsg_start = 100;
		txmsg_end = i;
		err = test_exec(cgrp, &opt);
		if (err)
			goto out;
	}

	/* Test start/end with cork pulling last sg entry */
	txmsg_start = 1500;
	txmsg_end = 1600;
	err = test_exec(cgrp, &opt);
	if (err)
		goto out;

	/* Test start/end pull of single byte in last page */
	txmsg_start = 1111;
	txmsg_end = 1112;
	err = test_exec(cgrp, &opt);
	if (err)
		goto out;

	/* Test start/end with end < start */
	txmsg_start = 1111;
	txmsg_end = 0;
	err = test_exec(cgrp, &opt);
	if (err)
		goto out;

	/* Test start/end with end > data */
	txmsg_start = 0;
	txmsg_end = 1601;
	err = test_exec(cgrp, &opt);
	if (err)
		goto out;

	/* Test start/end with start > data */
	txmsg_start = 1601;
	txmsg_end = 1600;
	err = test_exec(cgrp, &opt);

out:
	txmsg_start = 0;
	txmsg_end = 0;
	sched_yield();
	return err;
}

char *map_names[] = {
	"sock_map",
	"sock_map_txmsg",
	"sock_map_redir",
	"sock_apply_bytes",
	"sock_cork_bytes",
	"sock_pull_bytes",
	"sock_redir_flags",
	"sock_skb_opts",
};

int prog_attach_type[] = {
	BPF_SK_SKB_STREAM_PARSER,
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

static int __test_suite(char *bpf_file)
{
	int cg_fd, err;

	err = populate_progs(bpf_file);
	if (err < 0) {
		fprintf(stderr, "ERROR: (%i) load bpf failed\n", err);
		return err;
	}

	if (setup_cgroup_environment()) {
		fprintf(stderr, "ERROR: cgroup env failed\n");
		return -EINVAL;
	}

	cg_fd = create_and_get_cgroup(CG_PATH);
	if (cg_fd < 0) {
		fprintf(stderr,
			"ERROR: (%i) open cg path failed: %s\n",
			cg_fd, optarg);
		return cg_fd;
	}

	/* Tests basic commands and APIs with range of iov values */
	txmsg_start = txmsg_end = 0;
	err = test_txmsg(cg_fd);
	if (err)
		goto out;

	/* Tests interesting combinations of APIs used together */
	err = test_mixed(cg_fd);
	if (err)
		goto out;

	/* Tests pull_data API using start/end API */
	err = test_start_end(cg_fd);
	if (err)
		goto out;

out:
	printf("Summary: %i PASSED %i FAILED\n", passed, failed);
	cleanup_cgroup_environment();
	close(cg_fd);
	return err;
}

static int test_suite(void)
{
	int err;

	err = __test_suite(BPF_SOCKMAP_FILENAME);
	if (err)
		goto out;
	err = __test_suite(BPF_SOCKHASH_FILENAME);
out:
	return err;
}

int main(int argc, char **argv)
{
	struct rlimit r = {10 * 1024 * 1024, RLIM_INFINITY};
	int iov_count = 1, length = 1024, rate = 1;
	struct sockmap_options options = {0};
	int opt, longindex, err, cg_fd = 0;
	char *bpf_file = BPF_SOCKMAP_FILENAME;
	int test = PING_PONG;

	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK)");
		return 1;
	}

	if (argc < 2)
		return test_suite();

	while ((opt = getopt_long(argc, argv, ":dhvc:r:i:l:t:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 's':
			txmsg_start = atoi(optarg);
			break;
		case 'e':
			txmsg_end = atoi(optarg);
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
		case 0:
			break;
		case 'h':
		default:
			usage(argv);
			return -1;
		}
	}

	if (!cg_fd) {
		fprintf(stderr, "%s requires cgroup option: --cgroup <path>\n",
			argv[0]);
		return -1;
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
	close(cg_fd);
	return err;
}

void running_handler(int a)
{
	running = 0;
}
