// SPDX-License-Identifier: GPL-2.0
/* This is over-simplified TCP_REPAIR for TCP_ESTABLISHED sockets
 * It tests that TCP-AO enabled connection can be restored.
 * For the proper socket repair see:
 * https://github.com/checkpoint-restore/criu/blob/criu-dev/soccr/soccr.h
 */
#include <fcntl.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include "aolib.h"

#ifndef TCPOPT_MAXSEG
# define TCPOPT_MAXSEG		2
#endif
#ifndef TCPOPT_WINDOW
# define TCPOPT_WINDOW		3
#endif
#ifndef TCPOPT_SACK_PERMITTED
# define TCPOPT_SACK_PERMITTED	4
#endif
#ifndef TCPOPT_TIMESTAMP
# define TCPOPT_TIMESTAMP	8
#endif

enum {
	TCP_ESTABLISHED = 1,
	TCP_SYN_SENT,
	TCP_SYN_RECV,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_TIME_WAIT,
	TCP_CLOSE,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_LISTEN,
	TCP_CLOSING,	/* Now a valid state */
	TCP_NEW_SYN_RECV,

	TCP_MAX_STATES	/* Leave at the end! */
};

static void test_sock_checkpoint_queue(int sk, int queue, int qlen,
				       struct tcp_sock_queue *q)
{
	socklen_t len;
	int ret;

	if (setsockopt(sk, SOL_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue)))
		test_error("setsockopt(TCP_REPAIR_QUEUE)");

	len = sizeof(q->seq);
	ret = getsockopt(sk, SOL_TCP, TCP_QUEUE_SEQ, &q->seq, &len);
	if (ret || len != sizeof(q->seq))
		test_error("getsockopt(TCP_QUEUE_SEQ): %d", (int)len);

	if (!qlen) {
		q->buf = NULL;
		return;
	}

	q->buf = malloc(qlen);
	if (q->buf == NULL)
		test_error("malloc()");
	ret = recv(sk, q->buf, qlen, MSG_PEEK | MSG_DONTWAIT);
	if (ret != qlen)
		test_error("recv(%d): %d", qlen, ret);
}

void __test_sock_checkpoint(int sk, struct tcp_sock_state *state,
			    void *addr, size_t addr_size)
{
	socklen_t len = sizeof(state->info);
	int ret;

	memset(state, 0, sizeof(*state));

	ret = getsockopt(sk, SOL_TCP, TCP_INFO, &state->info, &len);
	if (ret || len != sizeof(state->info))
		test_error("getsockopt(TCP_INFO): %d", (int)len);

	len = addr_size;
	if (getsockname(sk, addr, &len) || len != addr_size)
		test_error("getsockname(): %d", (int)len);

	len = sizeof(state->trw);
	ret = getsockopt(sk, SOL_TCP, TCP_REPAIR_WINDOW, &state->trw, &len);
	if (ret || len != sizeof(state->trw))
		test_error("getsockopt(TCP_REPAIR_WINDOW): %d", (int)len);

	if (ioctl(sk, SIOCOUTQ, &state->outq_len))
		test_error("ioctl(SIOCOUTQ)");

	if (ioctl(sk, SIOCOUTQNSD, &state->outq_nsd_len))
		test_error("ioctl(SIOCOUTQNSD)");
	test_sock_checkpoint_queue(sk, TCP_SEND_QUEUE, state->outq_len, &state->out);

	if (ioctl(sk, SIOCINQ, &state->inq_len))
		test_error("ioctl(SIOCINQ)");
	test_sock_checkpoint_queue(sk, TCP_RECV_QUEUE, state->inq_len, &state->in);

	if (state->info.tcpi_state == TCP_CLOSE)
		state->outq_len = state->outq_nsd_len = 0;

	len = sizeof(state->mss);
	ret = getsockopt(sk, SOL_TCP, TCP_MAXSEG, &state->mss, &len);
	if (ret || len != sizeof(state->mss))
		test_error("getsockopt(TCP_MAXSEG): %d", (int)len);

	len = sizeof(state->timestamp);
	ret = getsockopt(sk, SOL_TCP, TCP_TIMESTAMP, &state->timestamp, &len);
	if (ret || len != sizeof(state->timestamp))
		test_error("getsockopt(TCP_TIMESTAMP): %d", (int)len);
}

void test_ao_checkpoint(int sk, struct tcp_ao_repair *state)
{
	socklen_t len = sizeof(*state);
	int ret;

	memset(state, 0, sizeof(*state));

	ret = getsockopt(sk, SOL_TCP, TCP_AO_REPAIR, state, &len);
	if (ret || len != sizeof(*state))
		test_error("getsockopt(TCP_AO_REPAIR): %d", (int)len);
}

static void test_sock_restore_seq(int sk, int queue, uint32_t seq)
{
	if (setsockopt(sk, SOL_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue)))
		test_error("setsockopt(TCP_REPAIR_QUEUE)");

	if (setsockopt(sk, SOL_TCP, TCP_QUEUE_SEQ, &seq, sizeof(seq)))
		test_error("setsockopt(TCP_QUEUE_SEQ)");
}

static void test_sock_restore_queue(int sk, int queue, void *buf, int len)
{
	int chunk = len;
	size_t off = 0;

	if (len == 0)
		return;

	if (setsockopt(sk, SOL_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue)))
		test_error("setsockopt(TCP_REPAIR_QUEUE)");

	do {
		int ret;

		ret = send(sk, buf + off, chunk, 0);
		if (ret <= 0) {
			if (chunk > 1024) {
				chunk >>= 1;
				continue;
			}
			test_error("send()");
		}
		off += ret;
		len -= ret;
	} while (len > 0);
}

void __test_sock_restore(int sk, const char *device,
			 struct tcp_sock_state *state,
			 void *saddr, void *daddr, size_t addr_size)
{
	struct tcp_repair_opt opts[4];
	unsigned int opt_nr = 0;
	long flags;

	if (bind(sk, saddr, addr_size))
		test_error("bind()");

	flags = fcntl(sk, F_GETFL);
	if ((flags < 0) || (fcntl(sk, F_SETFL, flags | O_NONBLOCK) < 0))
		test_error("fcntl()");

	test_sock_restore_seq(sk, TCP_RECV_QUEUE, state->in.seq - state->inq_len);
	test_sock_restore_seq(sk, TCP_SEND_QUEUE, state->out.seq - state->outq_len);

	if (device != NULL && setsockopt(sk, SOL_SOCKET, SO_BINDTODEVICE,
					 device, strlen(device) + 1))
		test_error("setsockopt(SO_BINDTODEVICE, %s)", device);

	if (connect(sk, daddr, addr_size))
		test_error("connect()");

	if (state->info.tcpi_options & TCPI_OPT_SACK) {
		opts[opt_nr].opt_code = TCPOPT_SACK_PERMITTED;
		opts[opt_nr].opt_val = 0;
		opt_nr++;
	}
	if (state->info.tcpi_options & TCPI_OPT_WSCALE) {
		opts[opt_nr].opt_code = TCPOPT_WINDOW;
		opts[opt_nr].opt_val = state->info.tcpi_snd_wscale +
				(state->info.tcpi_rcv_wscale << 16);
		opt_nr++;
	}
	if (state->info.tcpi_options & TCPI_OPT_TIMESTAMPS) {
		opts[opt_nr].opt_code = TCPOPT_TIMESTAMP;
		opts[opt_nr].opt_val = 0;
		opt_nr++;
	}
	opts[opt_nr].opt_code = TCPOPT_MAXSEG;
	opts[opt_nr].opt_val = state->mss;
	opt_nr++;

	if (setsockopt(sk, SOL_TCP, TCP_REPAIR_OPTIONS, opts, opt_nr * sizeof(opts[0])))
		test_error("setsockopt(TCP_REPAIR_OPTIONS)");

	if (state->info.tcpi_options & TCPI_OPT_TIMESTAMPS) {
		if (setsockopt(sk, SOL_TCP, TCP_TIMESTAMP,
			       &state->timestamp, opt_nr * sizeof(opts[0])))
			test_error("setsockopt(TCP_TIMESTAMP)");
	}
	test_sock_restore_queue(sk, TCP_RECV_QUEUE, state->in.buf, state->inq_len);
	test_sock_restore_queue(sk, TCP_SEND_QUEUE, state->out.buf, state->outq_len);
	if (setsockopt(sk, SOL_TCP, TCP_REPAIR_WINDOW, &state->trw, sizeof(state->trw)))
		test_error("setsockopt(TCP_REPAIR_WINDOW)");
}

void test_ao_restore(int sk, struct tcp_ao_repair *state)
{
	if (setsockopt(sk, SOL_TCP, TCP_AO_REPAIR, state, sizeof(*state)))
		test_error("setsockopt(TCP_AO_REPAIR)");
}

void test_sock_state_free(struct tcp_sock_state *state)
{
	free(state->out.buf);
	free(state->in.buf);
}

void test_enable_repair(int sk)
{
	int val = TCP_REPAIR_ON;

	if (setsockopt(sk, SOL_TCP, TCP_REPAIR, &val, sizeof(val)))
		test_error("setsockopt(TCP_REPAIR)");
}

void test_disable_repair(int sk)
{
	int val = TCP_REPAIR_OFF_NO_WP;

	if (setsockopt(sk, SOL_TCP, TCP_REPAIR, &val, sizeof(val)))
		test_error("setsockopt(TCP_REPAIR)");
}

void test_kill_sk(int sk)
{
	test_enable_repair(sk);
	close(sk);
}
