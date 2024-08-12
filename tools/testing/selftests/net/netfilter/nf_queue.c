// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>

struct options {
	bool count_packets;
	bool gso_enabled;
	int verbose;
	unsigned int queue_num;
	unsigned int timeout;
	uint32_t verdict;
	uint32_t delay_ms;
};

static unsigned int queue_stats[5];
static struct options opts;

static void help(const char *p)
{
	printf("Usage: %s [-c|-v [-vv] ] [-t timeout] [-q queue_num] [-Qdst_queue ] [ -d ms_delay ] [-G]\n", p);
}

static int parse_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	/* skip unsupported attribute in user-space */
	if (mnl_attr_type_valid(attr, NFQA_MAX) < 0)
		return MNL_CB_OK;

	switch (type) {
	case NFQA_MARK:
	case NFQA_IFINDEX_INDEV:
	case NFQA_IFINDEX_OUTDEV:
	case NFQA_IFINDEX_PHYSINDEV:
	case NFQA_IFINDEX_PHYSOUTDEV:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFQA_TIMESTAMP:
		if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC,
		    sizeof(struct nfqnl_msg_packet_timestamp)) < 0) {
			perror("mnl_attr_validate2");
			return MNL_CB_ERROR;
		}
		break;
	case NFQA_HWADDR:
		if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC,
		    sizeof(struct nfqnl_msg_packet_hw)) < 0) {
			perror("mnl_attr_validate2");
			return MNL_CB_ERROR;
		}
		break;
	case NFQA_PAYLOAD:
		break;
	}
	tb[type] = attr;
	return MNL_CB_OK;
}

static int queue_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nlattr *tb[NFQA_MAX+1] = { 0 };
	struct nfqnl_msg_packet_hdr *ph = NULL;
	uint32_t id = 0;

	(void)data;

	mnl_attr_parse(nlh, sizeof(struct nfgenmsg), parse_attr_cb, tb);
	if (tb[NFQA_PACKET_HDR]) {
		ph = mnl_attr_get_payload(tb[NFQA_PACKET_HDR]);
		id = ntohl(ph->packet_id);

		if (opts.verbose > 0)
			printf("packet hook=%u, hwproto 0x%x",
				ntohs(ph->hw_protocol), ph->hook);

		if (ph->hook >= 5) {
			fprintf(stderr, "Unknown hook %d\n", ph->hook);
			return MNL_CB_ERROR;
		}

		if (opts.verbose > 0) {
			uint32_t skbinfo = 0;

			if (tb[NFQA_SKB_INFO])
				skbinfo = ntohl(mnl_attr_get_u32(tb[NFQA_SKB_INFO]));
			if (skbinfo & NFQA_SKB_CSUMNOTREADY)
				printf(" csumnotready");
			if (skbinfo & NFQA_SKB_GSO)
				printf(" gso");
			if (skbinfo & NFQA_SKB_CSUM_NOTVERIFIED)
				printf(" csumnotverified");
			puts("");
		}

		if (opts.count_packets)
			queue_stats[ph->hook]++;
	}

	return MNL_CB_OK + id;
}

static struct nlmsghdr *
nfq_build_cfg_request(char *buf, uint8_t command, int queue_num)
{
	struct nlmsghdr *nlh = mnl_nlmsg_put_header(buf);
	struct nfqnl_msg_config_cmd cmd = {
		.command = command,
		.pf = htons(AF_INET),
	};
	struct nfgenmsg *nfg;

	nlh->nlmsg_type	= (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG;
	nlh->nlmsg_flags = NLM_F_REQUEST;

	nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(*nfg));

	nfg->nfgen_family = AF_UNSPEC;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(queue_num);

	mnl_attr_put(nlh, NFQA_CFG_CMD, sizeof(cmd), &cmd);

	return nlh;
}

static struct nlmsghdr *
nfq_build_cfg_params(char *buf, uint8_t mode, int range, int queue_num)
{
	struct nlmsghdr *nlh = mnl_nlmsg_put_header(buf);
	struct nfqnl_msg_config_params params = {
		.copy_range = htonl(range),
		.copy_mode = mode,
	};
	struct nfgenmsg *nfg;

	nlh->nlmsg_type	= (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG;
	nlh->nlmsg_flags = NLM_F_REQUEST;

	nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(*nfg));
	nfg->nfgen_family = AF_UNSPEC;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(queue_num);

	mnl_attr_put(nlh, NFQA_CFG_PARAMS, sizeof(params), &params);

	return nlh;
}

static struct nlmsghdr *
nfq_build_verdict(char *buf, int id, int queue_num, uint32_t verd)
{
	struct nfqnl_msg_verdict_hdr vh = {
		.verdict = htonl(verd),
		.id = htonl(id),
	};
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfg;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_VERDICT;
	nlh->nlmsg_flags = NLM_F_REQUEST;
	nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(*nfg));
	nfg->nfgen_family = AF_UNSPEC;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(queue_num);

	mnl_attr_put(nlh, NFQA_VERDICT_HDR, sizeof(vh), &vh);

	return nlh;
}

static void print_stats(void)
{
	unsigned int last, total;
	int i;

	total = 0;
	last = queue_stats[0];

	for (i = 0; i < 5; i++) {
		printf("hook %d packets %08u\n", i, queue_stats[i]);
		last = queue_stats[i];
		total += last;
	}

	printf("%u packets total\n", total);
}

struct mnl_socket *open_queue(void)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	unsigned int queue_num;
	struct mnl_socket *nl;
	struct nlmsghdr *nlh;
	struct timeval tv;
	uint32_t flags;

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}

	queue_num = opts.queue_num;
	nlh = nfq_build_cfg_request(buf, NFQNL_CFG_CMD_BIND, queue_num);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_sendto");
		exit(EXIT_FAILURE);
	}

	nlh = nfq_build_cfg_params(buf, NFQNL_COPY_PACKET, 0xFFFF, queue_num);

	flags = opts.gso_enabled ? NFQA_CFG_F_GSO : 0;
	flags |= NFQA_CFG_F_UID_GID;
	mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(flags));
	mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(flags));

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_sendto");
		exit(EXIT_FAILURE);
	}

	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = opts.timeout;
	if (opts.timeout && setsockopt(mnl_socket_get_fd(nl),
				       SOL_SOCKET, SO_RCVTIMEO,
				       &tv, sizeof(tv))) {
		perror("setsockopt(SO_RCVTIMEO)");
		exit(EXIT_FAILURE);
	}

	return nl;
}

static void sleep_ms(uint32_t delay)
{
	struct timespec ts = { .tv_sec = delay / 1000 };

	delay %= 1000;

	ts.tv_nsec = delay * 1000llu * 1000llu;

	nanosleep(&ts, NULL);
}

static int mainloop(void)
{
	unsigned int buflen = 64 * 1024 + MNL_SOCKET_BUFFER_SIZE;
	struct mnl_socket *nl;
	struct nlmsghdr *nlh;
	unsigned int portid;
	char *buf;
	int ret;

	buf = malloc(buflen);
	if (!buf) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	nl = open_queue();
	portid = mnl_socket_get_portid(nl);

	for (;;) {
		uint32_t id;

		ret = mnl_socket_recvfrom(nl, buf, buflen);
		if (ret == -1) {
			if (errno == ENOBUFS || errno == EINTR)
				continue;

			if (errno == EAGAIN) {
				errno = 0;
				ret = 0;
				break;
			}

			perror("mnl_socket_recvfrom");
			exit(EXIT_FAILURE);
		}

		ret = mnl_cb_run(buf, ret, 0, portid, queue_cb, NULL);
		if (ret < 0) {
			perror("mnl_cb_run");
			exit(EXIT_FAILURE);
		}

		id = ret - MNL_CB_OK;
		if (opts.delay_ms)
			sleep_ms(opts.delay_ms);

		nlh = nfq_build_verdict(buf, id, opts.queue_num, opts.verdict);
		if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
			perror("mnl_socket_sendto");
			exit(EXIT_FAILURE);
		}
	}

	mnl_socket_close(nl);

	return ret;
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "chvt:q:Q:d:G")) != -1) {
		switch (c) {
		case 'c':
			opts.count_packets = true;
			break;
		case 'h':
			help(argv[0]);
			exit(0);
			break;
		case 'q':
			opts.queue_num = atoi(optarg);
			if (opts.queue_num > 0xffff)
				opts.queue_num = 0;
			break;
		case 'Q':
			opts.verdict = atoi(optarg);
			if (opts.verdict > 0xffff) {
				fprintf(stderr, "Expected destination queue number\n");
				exit(1);
			}

			opts.verdict <<= 16;
			opts.verdict |= NF_QUEUE;
			break;
		case 'd':
			opts.delay_ms = atoi(optarg);
			if (opts.delay_ms == 0) {
				fprintf(stderr, "Expected nonzero delay (in milliseconds)\n");
				exit(1);
			}
			break;
		case 't':
			opts.timeout = atoi(optarg);
			break;
		case 'G':
			opts.gso_enabled = false;
			break;
		case 'v':
			opts.verbose++;
			break;
		}
	}

	if (opts.verdict != NF_ACCEPT && (opts.verdict >> 16 == opts.queue_num)) {
		fprintf(stderr, "Cannot use same destination and source queue\n");
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	opts.verdict = NF_ACCEPT;
	opts.gso_enabled = true;

	parse_opts(argc, argv);

	ret = mainloop();
	if (opts.count_packets)
		print_stats();

	return ret;
}
