// SPDX-License-Identifier: GPL-2.0

#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <argp.h>
#include "bench.h"
#include "bench_sockmap_prog.skel.h"
#include "bpf_util.h"

#define FILE_SIZE (128 * 1024)
#define DATA_REPEAT_SIZE 10

static const char snd_data[DATA_REPEAT_SIZE] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

/* c1 <-> [p1, p2] <-> c2
 * RX bench(BPF_SK_SKB_STREAM_VERDICT):
 *	ARG_FW_RX_PASS:
 *		send(p2) -> recv(c2) -> bpf skb passthrough -> recv(c2)
 *	ARG_FW_RX_VERDICT_EGRESS:
 *		send(c1) -> verdict skb to tx queuec of p2 -> recv(c2)
 *	ARG_FW_RX_VERDICT_INGRESS:
 *		send(c1) -> verdict skb to rx queuec of c2 -> recv(c2)
 *
 * TX bench(BPF_SK_MSG_VERDIC):
 *	ARG_FW_TX_PASS:
 *		send(p2) -> bpf msg passthrough -> send(p2) -> recv(c2)
 *	ARG_FW_TX_VERDICT_INGRESS:
 *		send(p2) -> verdict msg to rx queue of c2 -> recv(c2)
 *	ARG_FW_TX_VERDICT_EGRESS:
 *		send(p1) -> verdict msg to tx queue of p2 -> recv(c2)
 */
enum SOCKMAP_ARG_FLAG {
	ARG_FW_RX_NORMAL = 11000,
	ARG_FW_RX_PASS,
	ARG_FW_RX_VERDICT_EGRESS,
	ARG_FW_RX_VERDICT_INGRESS,
	ARG_FW_TX_NORMAL,
	ARG_FW_TX_PASS,
	ARG_FW_TX_VERDICT_INGRESS,
	ARG_FW_TX_VERDICT_EGRESS,
	ARG_CTL_RX_STRP,
	ARG_CONSUMER_DELAY_TIME,
	ARG_PRODUCER_DURATION,
};

#define TXMODE_NORMAL()				\
	((ctx.mode) == ARG_FW_TX_NORMAL)

#define TXMODE_BPF_INGRESS()			\
	((ctx.mode) == ARG_FW_TX_VERDICT_INGRESS)

#define TXMODE_BPF_EGRESS()			\
	((ctx.mode) == ARG_FW_TX_VERDICT_EGRESS)

#define TXMODE_BPF_PASS()			\
	((ctx.mode) == ARG_FW_TX_PASS)

#define TXMODE_BPF() (				\
	TXMODE_BPF_PASS() ||			\
	TXMODE_BPF_INGRESS() ||			\
	TXMODE_BPF_EGRESS())

#define TXMODE() (				\
	TXMODE_NORMAL() ||			\
	TXMODE_BPF())

#define RXMODE_NORMAL()				\
	((ctx.mode) == ARG_FW_RX_NORMAL)

#define RXMODE_BPF_PASS()			\
	((ctx.mode) == ARG_FW_RX_PASS)

#define RXMODE_BPF_VERDICT_EGRESS()		\
	((ctx.mode) == ARG_FW_RX_VERDICT_EGRESS)

#define RXMODE_BPF_VERDICT_INGRESS()		\
	((ctx.mode) == ARG_FW_RX_VERDICT_INGRESS)

#define RXMODE_BPF_VERDICT() (			\
	RXMODE_BPF_VERDICT_INGRESS() ||		\
	RXMODE_BPF_VERDICT_EGRESS())

#define RXMODE_BPF() (				\
	RXMODE_BPF_PASS() ||			\
	RXMODE_BPF_VERDICT())

#define RXMODE() (				\
	RXMODE_NORMAL() ||			\
	RXMODE_BPF())

static struct socmap_ctx {
	struct bench_sockmap_prog *skel;
	enum SOCKMAP_ARG_FLAG mode;
	#define c1	fds[0]
	#define p1	fds[1]
	#define c2	fds[2]
	#define p2	fds[3]
	#define sfd	fds[4]
	int		fds[5];
	long		send_calls;
	long		read_calls;
	long		prod_send;
	long		user_read;
	int		file_size;
	int		delay_consumer;
	int		prod_run_time;
	int		strp_size;
} ctx = {
	.prod_send	= 0,
	.user_read	= 0,
	.file_size	= FILE_SIZE,
	.mode		= ARG_FW_RX_VERDICT_EGRESS,
	.fds		= {0},
	.delay_consumer = 0,
	.prod_run_time	= 0,
	.strp_size	= 0,
};

static void bench_sockmap_prog_destroy(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ctx.fds); i++) {
		if (ctx.fds[i] > 0)
			close(ctx.fds[i]);
	}

	bench_sockmap_prog__destroy(ctx.skel);
}

static void init_addr(struct sockaddr_storage *ss,
		      socklen_t *len)
{
	struct sockaddr_in *addr4 = memset(ss, 0, sizeof(*ss));

	addr4->sin_family = AF_INET;
	addr4->sin_port = 0;
	addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	*len = sizeof(*addr4);
}

static bool set_non_block(int fd, bool blocking)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags == -1)
		return false;
	flags = blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	return (fcntl(fd, F_SETFL, flags) == 0);
}

static int create_pair(int *c, int *p, int type)
{
	struct sockaddr_storage addr;
	int err, cfd, pfd;
	socklen_t addr_len = sizeof(struct sockaddr_storage);

	err = getsockname(ctx.sfd, (struct sockaddr *)&addr, &addr_len);
	if (err) {
		fprintf(stderr, "getsockname error %d\n", errno);
		return err;
	}
	cfd = socket(AF_INET, type, 0);
	if (cfd < 0) {
		fprintf(stderr, "socket error %d\n", errno);
		return err;
	}

	err = connect(cfd, (struct sockaddr *)&addr, addr_len);
	if (err && errno != EINPROGRESS) {
		fprintf(stderr, "connect error %d\n", errno);
		return err;
	}

	pfd = accept(ctx.sfd, NULL, NULL);
	if (pfd < 0) {
		fprintf(stderr, "accept error %d\n", errno);
		return err;
	}
	*c = cfd;
	*p = pfd;
	return 0;
}

static int create_sockets(void)
{
	struct sockaddr_storage addr;
	int err, one = 1;
	socklen_t addr_len;

	init_addr(&addr, &addr_len);
	ctx.sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (ctx.sfd < 0) {
		fprintf(stderr, "socket error:%d\n", errno);
		return ctx.sfd;
	}
	err = setsockopt(ctx.sfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
	if (err) {
		fprintf(stderr, "setsockopt error:%d\n", errno);
		return err;
	}

	err = bind(ctx.sfd, (struct sockaddr *)&addr, addr_len);
	if (err) {
		fprintf(stderr, "bind error:%d\n", errno);
		return err;
	}

	err = listen(ctx.sfd, SOMAXCONN);
	if (err) {
		fprintf(stderr, "listen error:%d\n", errno);
		return err;
	}

	err = create_pair(&ctx.c1, &ctx.p1, SOCK_STREAM);
	if (err) {
		fprintf(stderr, "create_pair 1 error\n");
		return err;
	}

	err = create_pair(&ctx.c2, &ctx.p2, SOCK_STREAM);
	if (err) {
		fprintf(stderr, "create_pair 2 error\n");
		return err;
	}
	printf("create socket fd c1:%d p1:%d c2:%d p2:%d\n",
	       ctx.c1, ctx.p1, ctx.c2, ctx.p2);
	return 0;
}

static void validate(void)
{
	if (env.consumer_cnt != 2 || env.producer_cnt != 1 ||
	    !env.affinity)
		goto err;
	return;
err:
	fprintf(stderr, "argument '-c 2 -p 1 -a' is necessary");
	exit(1);
}

static int setup_rx_sockmap(void)
{
	int verdict, pass, parser, map;
	int zero = 0, one = 1;
	int err;

	parser = bpf_program__fd(ctx.skel->progs.prog_skb_parser);
	verdict = bpf_program__fd(ctx.skel->progs.prog_skb_verdict);
	pass = bpf_program__fd(ctx.skel->progs.prog_skb_pass);
	map = bpf_map__fd(ctx.skel->maps.sock_map_rx);

	if (ctx.strp_size != 0) {
		ctx.skel->bss->pkt_size = ctx.strp_size;
		err = bpf_prog_attach(parser, map, BPF_SK_SKB_STREAM_PARSER, 0);
		if (err)
			return err;
	}

	if (RXMODE_BPF_VERDICT())
		err = bpf_prog_attach(verdict, map, BPF_SK_SKB_STREAM_VERDICT, 0);
	else if (RXMODE_BPF_PASS())
		err = bpf_prog_attach(pass, map, BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err)
		return err;

	if (RXMODE_BPF_PASS())
		return bpf_map_update_elem(map, &zero, &ctx.c2, BPF_NOEXIST);

	err = bpf_map_update_elem(map, &zero, &ctx.p1, BPF_NOEXIST);
	if (err < 0)
		return err;

	if (RXMODE_BPF_VERDICT_INGRESS()) {
		ctx.skel->bss->verdict_dir = BPF_F_INGRESS;
		err = bpf_map_update_elem(map, &one, &ctx.c2, BPF_NOEXIST);
	} else {
		err = bpf_map_update_elem(map, &one, &ctx.p2, BPF_NOEXIST);
	}
	if (err < 0)
		return err;

	return 0;
}

static int setup_tx_sockmap(void)
{
	int zero = 0, one = 1;
	int prog, map;
	int err;

	map = bpf_map__fd(ctx.skel->maps.sock_map_tx);
	prog = TXMODE_BPF_PASS() ?
		bpf_program__fd(ctx.skel->progs.prog_skmsg_pass) :
		bpf_program__fd(ctx.skel->progs.prog_skmsg_verdict);

	err = bpf_prog_attach(prog, map, BPF_SK_MSG_VERDICT, 0);
	if (err)
		return err;

	if (TXMODE_BPF_EGRESS()) {
		err = bpf_map_update_elem(map, &zero, &ctx.p1, BPF_NOEXIST);
		err |= bpf_map_update_elem(map, &one, &ctx.p2, BPF_NOEXIST);
	} else {
		ctx.skel->bss->verdict_dir = BPF_F_INGRESS;
		err = bpf_map_update_elem(map, &zero, &ctx.p2, BPF_NOEXIST);
		err |= bpf_map_update_elem(map, &one, &ctx.c2, BPF_NOEXIST);
	}

	if (err < 0)
		return err;

	return 0;
}

static void setup(void)
{
	int err;

	ctx.skel = bench_sockmap_prog__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "error loading skel\n");
		exit(1);
	}

	if (create_sockets()) {
		fprintf(stderr, "create_net_mode error\n");
		goto err;
	}

	if (RXMODE_BPF()) {
		err = setup_rx_sockmap();
		if (err) {
			fprintf(stderr, "setup_rx_sockmap error:%d\n", err);
			goto err;
		}
	} else if (TXMODE_BPF()) {
		err = setup_tx_sockmap();
		if (err) {
			fprintf(stderr, "setup_tx_sockmap error:%d\n", err);
			goto err;
		}
	} else {
		fprintf(stderr, "unknown sockmap bench mode: %d\n", ctx.mode);
		goto err;
	}

	return;

err:
	bench_sockmap_prog_destroy();
	exit(1);
}

static void measure(struct bench_res *res)
{
	res->drops = atomic_swap(&ctx.prod_send, 0);
	res->hits = atomic_swap(&ctx.skel->bss->process_byte, 0);
	res->false_hits = atomic_swap(&ctx.user_read, 0);
	res->important_hits = atomic_swap(&ctx.send_calls, 0);
	res->important_hits |= atomic_swap(&ctx.read_calls, 0) << 32;
}

static void verify_data(int *check_pos, char *buf, int rcv)
{
	for (int i = 0 ; i < rcv; i++) {
		if (buf[i] != snd_data[(*check_pos) % DATA_REPEAT_SIZE]) {
			fprintf(stderr, "verify data fail");
			exit(1);
		}
		(*check_pos)++;
		if (*check_pos >= FILE_SIZE)
			*check_pos = 0;
	}
}

static void *consumer(void *input)
{
	int rcv, sent;
	int check_pos = 0;
	int tid = (long)input;
	int recv_buf_size = FILE_SIZE;
	char *buf = malloc(recv_buf_size);
	int delay_read = ctx.delay_consumer;

	if (!buf) {
		fprintf(stderr, "fail to init read buffer");
		return NULL;
	}

	while (true) {
		if (tid == 1) {
			/* consumer 1 is unused for tx test and stream verdict test */
			if (RXMODE_BPF() || TXMODE())
				return NULL;
			/* it's only for RX_NORMAL which service as reserve-proxy mode */
			rcv = read(ctx.p1, buf, recv_buf_size);
			if (rcv < 0) {
				fprintf(stderr, "fail to read p1");
				return NULL;
			}

			sent = send(ctx.p2, buf, recv_buf_size, 0);
			if (sent < 0) {
				fprintf(stderr, "fail to send p2");
				return NULL;
			}
		} else {
			if (delay_read != 0) {
				if (delay_read < 0)
					return NULL;
				sleep(delay_read);
				delay_read = 0;
			}
			/* read real endpoint by consumer 0 */
			atomic_inc(&ctx.read_calls);
			rcv = read(ctx.c2, buf, recv_buf_size);
			if (rcv < 0 && errno != EAGAIN) {
				fprintf(stderr, "%s fail to read c2 %d\n", __func__, errno);
				return NULL;
			}
			verify_data(&check_pos, buf, rcv);
			atomic_add(&ctx.user_read, rcv);
		}
	}

	return NULL;
}

static void *producer(void *input)
{
	int off = 0, fp, need_sent, sent;
	int file_size = ctx.file_size;
	struct timespec ts1, ts2;
	int target;
	FILE *file;

	file = tmpfile();
	if (!file) {
		fprintf(stderr, "create file for sendfile");
		return NULL;
	}

	/* we need simple verify */
	for (int i = 0; i < file_size; i++) {
		if (fwrite(&snd_data[off], sizeof(char), 1, file) != 1) {
			fprintf(stderr, "init tmpfile error");
			return NULL;
		}
		if (++off >= sizeof(snd_data))
			off = 0;
	}
	fflush(file);
	fseek(file, 0, SEEK_SET);

	fp = fileno(file);
	need_sent = file_size;
	clock_gettime(CLOCK_MONOTONIC, &ts1);

	if (RXMODE_BPF_VERDICT())
		target = ctx.c1;
	else if (TXMODE_BPF_EGRESS())
		target = ctx.p1;
	else
		target = ctx.p2;
	set_non_block(target, true);
	while (true) {
		if (ctx.prod_run_time) {
			clock_gettime(CLOCK_MONOTONIC, &ts2);
			if (ts2.tv_sec - ts1.tv_sec > ctx.prod_run_time)
				return NULL;
		}

		errno = 0;
		atomic_inc(&ctx.send_calls);
		sent = sendfile(target, fp, NULL, need_sent);
		if (sent < 0) {
			if (errno != EAGAIN && errno != ENOMEM && errno != ENOBUFS) {
				fprintf(stderr, "sendfile return %d, errorno %d:%s\n",
					sent, errno, strerror(errno));
				return NULL;
			}
			continue;
		} else if (sent < need_sent) {
			need_sent -= sent;
			atomic_add(&ctx.prod_send, sent);
			continue;
		}
		atomic_add(&ctx.prod_send, need_sent);
		need_sent = file_size;
		lseek(fp, 0, SEEK_SET);
	}

	return NULL;
}

static void report_progress(int iter, struct bench_res *res, long delta_ns)
{
	double speed_mbs, prod_mbs, bpf_mbs, send_hz, read_hz;

	prod_mbs = res->drops / 1000000.0 / (delta_ns / 1000000000.0);
	speed_mbs = res->false_hits / 1000000.0 / (delta_ns / 1000000000.0);
	bpf_mbs = res->hits / 1000000.0 / (delta_ns / 1000000000.0);
	send_hz = (res->important_hits & 0xFFFFFFFF) / (delta_ns / 1000000000.0);
	read_hz = (res->important_hits >> 32) / (delta_ns / 1000000000.0);

	printf("Iter %3d (%7.3lfus): ",
	       iter, (delta_ns - 1000000000) / 1000.0);
	printf("Send Speed %8.3lf MB/s (%8.3lf calls/s), BPF Speed %8.3lf MB/s, "
	       "Rcv Speed %8.3lf MB/s (%8.3lf calls/s)\n",
	       prod_mbs, send_hz, bpf_mbs, speed_mbs, read_hz);
}

static void report_final(struct bench_res res[], int res_cnt)
{
	double verdict_mbs_mean = 0.0;
	long verdict_total = 0;
	int i;

	for (i = 0; i < res_cnt; i++) {
		verdict_mbs_mean += res[i].hits / 1000000.0 / (0.0 + res_cnt);
		verdict_total += res[i].hits / 1000000.0;
	}

	printf("Summary: total trans %8.3lu MB \u00B1 %5.3lf MB/s\n",
	       verdict_total, verdict_mbs_mean);
}

static const struct argp_option opts[] = {
	{ "rx-normal", ARG_FW_RX_NORMAL, NULL, 0,
		"simple reserve-proxy mode, no bfp enabled"},
	{ "rx-pass", ARG_FW_RX_PASS, NULL, 0,
		"run bpf prog but no redir applied"},
	{ "rx-strp", ARG_CTL_RX_STRP, "Byte", 0,
		"enable strparser and set the encapsulation size"},
	{ "rx-verdict-egress", ARG_FW_RX_VERDICT_EGRESS, NULL, 0,
		"forward data with bpf(stream verdict)"},
	{ "rx-verdict-ingress", ARG_FW_RX_VERDICT_INGRESS, NULL, 0,
		"forward data with bpf(stream verdict)"},
	{ "tx-normal", ARG_FW_TX_NORMAL, NULL, 0,
		"simple c-s mode, no bfp enabled"},
	{ "tx-pass", ARG_FW_TX_PASS, NULL, 0,
		"run bpf prog but no redir applied"},
	{ "tx-verdict-ingress", ARG_FW_TX_VERDICT_INGRESS, NULL, 0,
		"forward msg to ingress queue of another socket"},
	{ "tx-verdict-egress", ARG_FW_TX_VERDICT_EGRESS, NULL, 0,
		"forward msg to egress queue of another socket"},
	{ "delay-consumer", ARG_CONSUMER_DELAY_TIME, "SEC", 0,
		"delay consumer start"},
	{ "producer-duration", ARG_PRODUCER_DURATION, "SEC", 0,
		"producer duration"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_FW_RX_NORMAL...ARG_FW_TX_VERDICT_EGRESS:
		ctx.mode = key;
		break;
	case ARG_CONSUMER_DELAY_TIME:
		ctx.delay_consumer = strtol(arg, NULL, 10);
		break;
	case ARG_PRODUCER_DURATION:
		ctx.prod_run_time = strtol(arg, NULL, 10);
		break;
	case ARG_CTL_RX_STRP:
		ctx.strp_size = strtol(arg, NULL, 10);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

/* exported into benchmark runner */
const struct argp bench_sockmap_argp = {
	.options	= opts,
	.parser		= parse_arg,
};

/* Benchmark performance of creating bpf local storage  */
const struct bench bench_sockmap = {
	.name			= "sockmap",
	.argp			= &bench_sockmap_argp,
	.validate		= validate,
	.setup			= setup,
	.producer_thread	= producer,
	.consumer_thread	= consumer,
	.measure		= measure,
	.report_progress	= report_progress,
	.report_final		= report_final,
};
