// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Justin Iurman (justin.iurman@uliege.be)
 *
 * IOAM parser for IPv6, see ioam6.sh for details.
 */
#include <asm/byteorder.h>
#include <linux/const.h>
#include <linux/if_ether.h>
#include <linux/ioam6.h>
#include <linux/ipv6.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct node_args {
	__u32 id;
	__u64 wide;
	__u16 ingr_id;
	__u16 egr_id;
	__u32 ingr_wide;
	__u32 egr_wide;
	__u32 ns_data;
	__u64 ns_wide;
	__u32 sc_id;
	__u8 hop_limit;
	__u8 *sc_data; /* NULL when sc_id = 0xffffff (default empty value) */
};

/* expected args per node, in that order */
enum {
	NODE_ARG_HOP_LIMIT,
	NODE_ARG_ID,
	NODE_ARG_WIDE,
	NODE_ARG_INGR_ID,
	NODE_ARG_INGR_WIDE,
	NODE_ARG_EGR_ID,
	NODE_ARG_EGR_WIDE,
	NODE_ARG_NS_DATA,
	NODE_ARG_NS_WIDE,
	NODE_ARG_SC_ID,
	__NODE_ARG_MAX,
};

#define NODE_ARGS_SIZE __NODE_ARG_MAX

struct args {
	__u16 ns_id;
	__u32 trace_type;
	__u8 n_node;
	__u8 *ifname;
	struct node_args node[0];
};

/* expected args, in that order */
enum {
	ARG_IFNAME,
	ARG_N_NODE,
	ARG_NS_ID,
	ARG_TRACE_TYPE,
	__ARG_MAX,
};

#define ARGS_SIZE __ARG_MAX

int check_ioam6_node_data(__u8 **p, struct ioam6_trace_hdr *trace, __u8 hlim,
			  __u32 id, __u64 wide, __u16 ingr_id, __u32 ingr_wide,
			  __u16 egr_id, __u32 egr_wide, __u32 ns_data,
			  __u64 ns_wide, __u32 sc_id, __u8 *sc_data)
{
	__u64 raw64;
	__u32 raw32;
	__u8 sc_len;

	if (trace->type.bit0) {
		raw32 = __be32_to_cpu(*((__u32 *)*p));
		if (hlim != (raw32 >> 24) || id != (raw32 & 0xffffff))
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit1) {
		raw32 = __be32_to_cpu(*((__u32 *)*p));
		if (ingr_id != (raw32 >> 16) || egr_id != (raw32 & 0xffff))
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit2)
		*p += sizeof(__u32);

	if (trace->type.bit3)
		*p += sizeof(__u32);

	if (trace->type.bit4) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit5) {
		if (__be32_to_cpu(*((__u32 *)*p)) != ns_data)
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit6) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit7) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit8) {
		raw64 = __be64_to_cpu(*((__u64 *)*p));
		if (hlim != (raw64 >> 56) || wide != (raw64 & 0xffffffffffffff))
			return 1;
		*p += sizeof(__u64);
	}

	if (trace->type.bit9) {
		if (__be32_to_cpu(*((__u32 *)*p)) != ingr_wide)
			return 1;
		*p += sizeof(__u32);

		if (__be32_to_cpu(*((__u32 *)*p)) != egr_wide)
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit10) {
		if (__be64_to_cpu(*((__u64 *)*p)) != ns_wide)
			return 1;
		*p += sizeof(__u64);
	}

	if (trace->type.bit11) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (trace->type.bit22) {
		raw32 = __be32_to_cpu(*((__u32 *)*p));
		sc_len = sc_data ? __ALIGN_KERNEL(strlen(sc_data), 4) : 0;
		if (sc_len != (raw32 >> 24) * 4 || sc_id != (raw32 & 0xffffff))
			return 1;
		*p += sizeof(__u32);

		if (sc_data) {
			if (strncmp(*p, sc_data, strlen(sc_data)))
				return 1;

			*p += strlen(sc_data);
			sc_len -= strlen(sc_data);

			while (sc_len--) {
				if (**p != '\0')
					return 1;
				*p += sizeof(__u8);
			}
		}
	}

	return 0;
}

int check_ioam6_trace(struct ioam6_trace_hdr *trace, struct args *args)
{
	__u8 *p;
	int i;

	if (__be16_to_cpu(trace->namespace_id) != args->ns_id ||
	    __be32_to_cpu(trace->type_be32) != args->trace_type)
		return 1;

	p = trace->data + trace->remlen * 4;

	for (i = args->n_node - 1; i >= 0; i--) {
		if (check_ioam6_node_data(&p, trace,
					  args->node[i].hop_limit,
					  args->node[i].id,
					  args->node[i].wide,
					  args->node[i].ingr_id,
					  args->node[i].ingr_wide,
					  args->node[i].egr_id,
					  args->node[i].egr_wide,
					  args->node[i].ns_data,
					  args->node[i].ns_wide,
					  args->node[i].sc_id,
					  args->node[i].sc_data))
			return 1;
	}

	return 0;
}

int parse_node_args(int *argcp, char ***argvp, struct node_args *node)
{
	char **argv = *argvp;

	if (*argcp < NODE_ARGS_SIZE)
		return 1;

	node->hop_limit = strtoul(argv[NODE_ARG_HOP_LIMIT], NULL, 10);
	if (!node->hop_limit) {
		node->hop_limit = strtoul(argv[NODE_ARG_HOP_LIMIT], NULL, 16);
		if (!node->hop_limit)
			return 1;
	}

	node->id = strtoul(argv[NODE_ARG_ID], NULL, 10);
	if (!node->id) {
		node->id = strtoul(argv[NODE_ARG_ID], NULL, 16);
		if (!node->id)
			return 1;
	}

	node->wide = strtoull(argv[NODE_ARG_WIDE], NULL, 10);
	if (!node->wide) {
		node->wide = strtoull(argv[NODE_ARG_WIDE], NULL, 16);
		if (!node->wide)
			return 1;
	}

	node->ingr_id = strtoul(argv[NODE_ARG_INGR_ID], NULL, 10);
	if (!node->ingr_id) {
		node->ingr_id = strtoul(argv[NODE_ARG_INGR_ID], NULL, 16);
		if (!node->ingr_id)
			return 1;
	}

	node->ingr_wide = strtoul(argv[NODE_ARG_INGR_WIDE], NULL, 10);
	if (!node->ingr_wide) {
		node->ingr_wide = strtoul(argv[NODE_ARG_INGR_WIDE], NULL, 16);
		if (!node->ingr_wide)
			return 1;
	}

	node->egr_id = strtoul(argv[NODE_ARG_EGR_ID], NULL, 10);
	if (!node->egr_id) {
		node->egr_id = strtoul(argv[NODE_ARG_EGR_ID], NULL, 16);
		if (!node->egr_id)
			return 1;
	}

	node->egr_wide = strtoul(argv[NODE_ARG_EGR_WIDE], NULL, 10);
	if (!node->egr_wide) {
		node->egr_wide = strtoul(argv[NODE_ARG_EGR_WIDE], NULL, 16);
		if (!node->egr_wide)
			return 1;
	}

	node->ns_data = strtoul(argv[NODE_ARG_NS_DATA], NULL, 16);
	if (!node->ns_data)
		return 1;

	node->ns_wide = strtoull(argv[NODE_ARG_NS_WIDE], NULL, 16);
	if (!node->ns_wide)
		return 1;

	node->sc_id = strtoul(argv[NODE_ARG_SC_ID], NULL, 10);
	if (!node->sc_id) {
		node->sc_id = strtoul(argv[NODE_ARG_SC_ID], NULL, 16);
		if (!node->sc_id)
			return 1;
	}

	*argcp -= NODE_ARGS_SIZE;
	*argvp += NODE_ARGS_SIZE;

	if (node->sc_id != 0xffffff) {
		if (!*argcp)
			return 1;

		node->sc_data = argv[NODE_ARG_SC_ID + 1];

		*argcp -= 1;
		*argvp += 1;
	}

	return 0;
}

struct args *parse_args(int argc, char **argv)
{
	struct args *args;
	int n_node, i;

	if (argc < ARGS_SIZE)
		goto out;

	n_node = strtoul(argv[ARG_N_NODE], NULL, 10);
	if (!n_node || n_node > 10)
		goto out;

	args = calloc(1, sizeof(*args) + n_node * sizeof(struct node_args));
	if (!args)
		goto out;

	args->ns_id = strtoul(argv[ARG_NS_ID], NULL, 10);
	if (!args->ns_id)
		goto free;

	args->trace_type = strtoul(argv[ARG_TRACE_TYPE], NULL, 16);
	if (!args->trace_type)
		goto free;

	args->n_node = n_node;
	args->ifname = argv[ARG_IFNAME];

	argv += ARGS_SIZE;
	argc -= ARGS_SIZE;

	for (i = 0; i < n_node; i++) {
		if (parse_node_args(&argc, &argv, &args->node[i]))
			goto free;
	}

	if (argc)
		goto free;

	return args;
free:
	free(args);
out:
	return NULL;
}

int main(int argc, char **argv)
{
	int ret, fd, pkts, size, hoplen, found;
	struct ioam6_trace_hdr *ioam6h;
	struct ioam6_hdr *opt;
	struct ipv6hdr *ip6h;
	__u8 buffer[400], *p;
	struct args *args;

	args = parse_args(argc - 1, argv + 1);
	if (!args) {
		ret = 1;
		goto out;
	}

	fd = socket(AF_PACKET, SOCK_DGRAM, __cpu_to_be16(ETH_P_IPV6));
	if (!fd) {
		ret = 1;
		goto out;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
		       args->ifname, strlen(args->ifname))) {
		ret = 1;
		goto close;
	}

	pkts = 0;
	found = 0;
	while (pkts < 3 && !found) {
		size = recv(fd, buffer, sizeof(buffer), 0);
		ip6h = (struct ipv6hdr *)buffer;
		pkts++;

		if (ip6h->nexthdr == IPPROTO_HOPOPTS) {
			p = buffer + sizeof(*ip6h);
			hoplen = (p[1] + 1) << 3;

			p += sizeof(struct ipv6_hopopt_hdr);
			while (hoplen > 0) {
				opt = (struct ioam6_hdr *)p;

				if (opt->opt_type == IPV6_TLV_IOAM &&
				    opt->type == IOAM6_TYPE_PREALLOC) {
					found = 1;

					p += sizeof(*opt);
					ioam6h = (struct ioam6_trace_hdr *)p;

					ret = check_ioam6_trace(ioam6h, args);
					break;
				}

				p += opt->opt_len + 2;
				hoplen -= opt->opt_len + 2;
			}
		}
	}

	if (!found)
		ret = 1;
close:
	close(fd);
out:
	free(args);
	return ret;
}
