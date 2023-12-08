// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Justin Iurman (justin.iurman@uliege.be)
 *
 * IOAM tester for IPv6, see ioam6.sh for details on each test case.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <linux/const.h>
#include <linux/if_ether.h>
#include <linux/ioam6.h>
#include <linux/ipv6.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ioam_config {
	__u32 id;
	__u64 wide;
	__u16 ingr_id;
	__u16 egr_id;
	__u32 ingr_wide;
	__u32 egr_wide;
	__u32 ns_data;
	__u64 ns_wide;
	__u32 sc_id;
	__u8 hlim;
	char *sc_data;
};

/*
 * Be careful if you modify structs below - everything MUST be kept synchronized
 * with configurations inside ioam6.sh and always reflect the same.
 */

static struct ioam_config node1 = {
	.id = 1,
	.wide = 11111111,
	.ingr_id = 0xffff, /* default value */
	.egr_id = 101,
	.ingr_wide = 0xffffffff, /* default value */
	.egr_wide = 101101,
	.ns_data = 0xdeadbee0,
	.ns_wide = 0xcafec0caf00dc0de,
	.sc_id = 777,
	.sc_data = "something that will be 4n-aligned",
	.hlim = 64,
};

static struct ioam_config node2 = {
	.id = 2,
	.wide = 22222222,
	.ingr_id = 201,
	.egr_id = 202,
	.ingr_wide = 201201,
	.egr_wide = 202202,
	.ns_data = 0xdeadbee1,
	.ns_wide = 0xcafec0caf11dc0de,
	.sc_id = 666,
	.sc_data = "Hello there -Obi",
	.hlim = 63,
};

static struct ioam_config node3 = {
	.id = 3,
	.wide = 33333333,
	.ingr_id = 301,
	.egr_id = 0xffff, /* default value */
	.ingr_wide = 301301,
	.egr_wide = 0xffffffff, /* default value */
	.ns_data = 0xdeadbee2,
	.ns_wide = 0xcafec0caf22dc0de,
	.sc_id = 0xffffff, /* default value */
	.sc_data = NULL,
	.hlim = 62,
};

enum {
	/**********
	 * OUTPUT *
	 **********/
	TEST_OUT_UNDEF_NS,
	TEST_OUT_NO_ROOM,
	TEST_OUT_BIT0,
	TEST_OUT_BIT1,
	TEST_OUT_BIT2,
	TEST_OUT_BIT3,
	TEST_OUT_BIT4,
	TEST_OUT_BIT5,
	TEST_OUT_BIT6,
	TEST_OUT_BIT7,
	TEST_OUT_BIT8,
	TEST_OUT_BIT9,
	TEST_OUT_BIT10,
	TEST_OUT_BIT11,
	TEST_OUT_BIT22,
	TEST_OUT_FULL_SUPP_TRACE,

	/*********
	 * INPUT *
	 *********/
	TEST_IN_UNDEF_NS,
	TEST_IN_NO_ROOM,
	TEST_IN_OFLAG,
	TEST_IN_BIT0,
	TEST_IN_BIT1,
	TEST_IN_BIT2,
	TEST_IN_BIT3,
	TEST_IN_BIT4,
	TEST_IN_BIT5,
	TEST_IN_BIT6,
	TEST_IN_BIT7,
	TEST_IN_BIT8,
	TEST_IN_BIT9,
	TEST_IN_BIT10,
	TEST_IN_BIT11,
	TEST_IN_BIT22,
	TEST_IN_FULL_SUPP_TRACE,

	/**********
	 * GLOBAL *
	 **********/
	TEST_FWD_FULL_SUPP_TRACE,

	__TEST_MAX,
};

static int check_ioam_header(int tid, struct ioam6_trace_hdr *ioam6h,
			     __u32 trace_type, __u16 ioam_ns)
{
	if (__be16_to_cpu(ioam6h->namespace_id) != ioam_ns ||
	    __be32_to_cpu(ioam6h->type_be32) != (trace_type << 8))
		return 1;

	switch (tid) {
	case TEST_OUT_UNDEF_NS:
	case TEST_IN_UNDEF_NS:
		return ioam6h->overflow ||
		       ioam6h->nodelen != 1 ||
		       ioam6h->remlen != 1;

	case TEST_OUT_NO_ROOM:
	case TEST_IN_NO_ROOM:
	case TEST_IN_OFLAG:
		return !ioam6h->overflow ||
		       ioam6h->nodelen != 2 ||
		       ioam6h->remlen != 1;

	case TEST_OUT_BIT0:
	case TEST_IN_BIT0:
	case TEST_OUT_BIT1:
	case TEST_IN_BIT1:
	case TEST_OUT_BIT2:
	case TEST_IN_BIT2:
	case TEST_OUT_BIT3:
	case TEST_IN_BIT3:
	case TEST_OUT_BIT4:
	case TEST_IN_BIT4:
	case TEST_OUT_BIT5:
	case TEST_IN_BIT5:
	case TEST_OUT_BIT6:
	case TEST_IN_BIT6:
	case TEST_OUT_BIT7:
	case TEST_IN_BIT7:
	case TEST_OUT_BIT11:
	case TEST_IN_BIT11:
		return ioam6h->overflow ||
		       ioam6h->nodelen != 1 ||
		       ioam6h->remlen;

	case TEST_OUT_BIT8:
	case TEST_IN_BIT8:
	case TEST_OUT_BIT9:
	case TEST_IN_BIT9:
	case TEST_OUT_BIT10:
	case TEST_IN_BIT10:
		return ioam6h->overflow ||
		       ioam6h->nodelen != 2 ||
		       ioam6h->remlen;

	case TEST_OUT_BIT22:
	case TEST_IN_BIT22:
		return ioam6h->overflow ||
		       ioam6h->nodelen ||
		       ioam6h->remlen;

	case TEST_OUT_FULL_SUPP_TRACE:
	case TEST_IN_FULL_SUPP_TRACE:
	case TEST_FWD_FULL_SUPP_TRACE:
		return ioam6h->overflow ||
		       ioam6h->nodelen != 15 ||
		       ioam6h->remlen;

	default:
		break;
	}

	return 1;
}

static int check_ioam6_data(__u8 **p, struct ioam6_trace_hdr *ioam6h,
			    const struct ioam_config cnf)
{
	unsigned int len;
	__u8 aligned;
	__u64 raw64;
	__u32 raw32;

	if (ioam6h->type.bit0) {
		raw32 = __be32_to_cpu(*((__u32 *)*p));
		if (cnf.hlim != (raw32 >> 24) || cnf.id != (raw32 & 0xffffff))
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit1) {
		raw32 = __be32_to_cpu(*((__u32 *)*p));
		if (cnf.ingr_id != (raw32 >> 16) ||
		    cnf.egr_id != (raw32 & 0xffff))
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit2)
		*p += sizeof(__u32);

	if (ioam6h->type.bit3)
		*p += sizeof(__u32);

	if (ioam6h->type.bit4) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit5) {
		if (__be32_to_cpu(*((__u32 *)*p)) != cnf.ns_data)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit6)
		*p += sizeof(__u32);

	if (ioam6h->type.bit7) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit8) {
		raw64 = __be64_to_cpu(*((__u64 *)*p));
		if (cnf.hlim != (raw64 >> 56) ||
		    cnf.wide != (raw64 & 0xffffffffffffff))
			return 1;
		*p += sizeof(__u64);
	}

	if (ioam6h->type.bit9) {
		if (__be32_to_cpu(*((__u32 *)*p)) != cnf.ingr_wide)
			return 1;
		*p += sizeof(__u32);

		if (__be32_to_cpu(*((__u32 *)*p)) != cnf.egr_wide)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit10) {
		if (__be64_to_cpu(*((__u64 *)*p)) != cnf.ns_wide)
			return 1;
		*p += sizeof(__u64);
	}

	if (ioam6h->type.bit11) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit12) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit13) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit14) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit15) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit16) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit17) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit18) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit19) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit20) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit21) {
		if (__be32_to_cpu(*((__u32 *)*p)) != 0xffffffff)
			return 1;
		*p += sizeof(__u32);
	}

	if (ioam6h->type.bit22) {
		len = cnf.sc_data ? strlen(cnf.sc_data) : 0;
		aligned = cnf.sc_data ? __ALIGN_KERNEL(len, 4) : 0;

		raw32 = __be32_to_cpu(*((__u32 *)*p));
		if (aligned != (raw32 >> 24) * 4 ||
		    cnf.sc_id != (raw32 & 0xffffff))
			return 1;
		*p += sizeof(__u32);

		if (cnf.sc_data) {
			if (strncmp((char *)*p, cnf.sc_data, len))
				return 1;

			*p += len;
			aligned -= len;

			while (aligned--) {
				if (**p != '\0')
					return 1;
				*p += sizeof(__u8);
			}
		}
	}

	return 0;
}

static int check_ioam_header_and_data(int tid, struct ioam6_trace_hdr *ioam6h,
				      __u32 trace_type, __u16 ioam_ns)
{
	__u8 *p;

	if (check_ioam_header(tid, ioam6h, trace_type, ioam_ns))
		return 1;

	p = ioam6h->data + ioam6h->remlen * 4;

	switch (tid) {
	case TEST_OUT_BIT0:
	case TEST_OUT_BIT1:
	case TEST_OUT_BIT2:
	case TEST_OUT_BIT3:
	case TEST_OUT_BIT4:
	case TEST_OUT_BIT5:
	case TEST_OUT_BIT6:
	case TEST_OUT_BIT7:
	case TEST_OUT_BIT8:
	case TEST_OUT_BIT9:
	case TEST_OUT_BIT10:
	case TEST_OUT_BIT11:
	case TEST_OUT_BIT22:
	case TEST_OUT_FULL_SUPP_TRACE:
		return check_ioam6_data(&p, ioam6h, node1);

	case TEST_IN_BIT0:
	case TEST_IN_BIT1:
	case TEST_IN_BIT2:
	case TEST_IN_BIT3:
	case TEST_IN_BIT4:
	case TEST_IN_BIT5:
	case TEST_IN_BIT6:
	case TEST_IN_BIT7:
	case TEST_IN_BIT8:
	case TEST_IN_BIT9:
	case TEST_IN_BIT10:
	case TEST_IN_BIT11:
	case TEST_IN_BIT22:
	case TEST_IN_FULL_SUPP_TRACE:
	{
		__u32 tmp32 = node2.egr_wide;
		__u16 tmp16 = node2.egr_id;
		int res;

		node2.egr_id = 0xffff;
		node2.egr_wide = 0xffffffff;

		res = check_ioam6_data(&p, ioam6h, node2);

		node2.egr_id = tmp16;
		node2.egr_wide = tmp32;

		return res;
	}

	case TEST_FWD_FULL_SUPP_TRACE:
		if (check_ioam6_data(&p, ioam6h, node3))
			return 1;
		if (check_ioam6_data(&p, ioam6h, node2))
			return 1;
		return check_ioam6_data(&p, ioam6h, node1);

	default:
		break;
	}

	return 1;
}

static int str2id(const char *tname)
{
	if (!strcmp("out_undef_ns", tname))
		return TEST_OUT_UNDEF_NS;
	if (!strcmp("out_no_room", tname))
		return TEST_OUT_NO_ROOM;
	if (!strcmp("out_bit0", tname))
		return TEST_OUT_BIT0;
	if (!strcmp("out_bit1", tname))
		return TEST_OUT_BIT1;
	if (!strcmp("out_bit2", tname))
		return TEST_OUT_BIT2;
	if (!strcmp("out_bit3", tname))
		return TEST_OUT_BIT3;
	if (!strcmp("out_bit4", tname))
		return TEST_OUT_BIT4;
	if (!strcmp("out_bit5", tname))
		return TEST_OUT_BIT5;
	if (!strcmp("out_bit6", tname))
		return TEST_OUT_BIT6;
	if (!strcmp("out_bit7", tname))
		return TEST_OUT_BIT7;
	if (!strcmp("out_bit8", tname))
		return TEST_OUT_BIT8;
	if (!strcmp("out_bit9", tname))
		return TEST_OUT_BIT9;
	if (!strcmp("out_bit10", tname))
		return TEST_OUT_BIT10;
	if (!strcmp("out_bit11", tname))
		return TEST_OUT_BIT11;
	if (!strcmp("out_bit22", tname))
		return TEST_OUT_BIT22;
	if (!strcmp("out_full_supp_trace", tname))
		return TEST_OUT_FULL_SUPP_TRACE;
	if (!strcmp("in_undef_ns", tname))
		return TEST_IN_UNDEF_NS;
	if (!strcmp("in_no_room", tname))
		return TEST_IN_NO_ROOM;
	if (!strcmp("in_oflag", tname))
		return TEST_IN_OFLAG;
	if (!strcmp("in_bit0", tname))
		return TEST_IN_BIT0;
	if (!strcmp("in_bit1", tname))
		return TEST_IN_BIT1;
	if (!strcmp("in_bit2", tname))
		return TEST_IN_BIT2;
	if (!strcmp("in_bit3", tname))
		return TEST_IN_BIT3;
	if (!strcmp("in_bit4", tname))
		return TEST_IN_BIT4;
	if (!strcmp("in_bit5", tname))
		return TEST_IN_BIT5;
	if (!strcmp("in_bit6", tname))
		return TEST_IN_BIT6;
	if (!strcmp("in_bit7", tname))
		return TEST_IN_BIT7;
	if (!strcmp("in_bit8", tname))
		return TEST_IN_BIT8;
	if (!strcmp("in_bit9", tname))
		return TEST_IN_BIT9;
	if (!strcmp("in_bit10", tname))
		return TEST_IN_BIT10;
	if (!strcmp("in_bit11", tname))
		return TEST_IN_BIT11;
	if (!strcmp("in_bit22", tname))
		return TEST_IN_BIT22;
	if (!strcmp("in_full_supp_trace", tname))
		return TEST_IN_FULL_SUPP_TRACE;
	if (!strcmp("fwd_full_supp_trace", tname))
		return TEST_FWD_FULL_SUPP_TRACE;

	return -1;
}

static int ipv6_addr_equal(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return ((a1->s6_addr32[0] ^ a2->s6_addr32[0]) |
		(a1->s6_addr32[1] ^ a2->s6_addr32[1]) |
		(a1->s6_addr32[2] ^ a2->s6_addr32[2]) |
		(a1->s6_addr32[3] ^ a2->s6_addr32[3])) == 0;
}

static int get_u32(__u32 *val, const char *arg, int base)
{
	unsigned long res;
	char *ptr;

	if (!arg || !*arg)
		return -1;
	res = strtoul(arg, &ptr, base);

	if (!ptr || ptr == arg || *ptr)
		return -1;

	if (res == ULONG_MAX && errno == ERANGE)
		return -1;

	if (res > 0xFFFFFFFFUL)
		return -1;

	*val = res;
	return 0;
}

static int get_u16(__u16 *val, const char *arg, int base)
{
	unsigned long res;
	char *ptr;

	if (!arg || !*arg)
		return -1;
	res = strtoul(arg, &ptr, base);

	if (!ptr || ptr == arg || *ptr)
		return -1;

	if (res == ULONG_MAX && errno == ERANGE)
		return -1;

	if (res > 0xFFFFUL)
		return -1;

	*val = res;
	return 0;
}

static int (*func[__TEST_MAX])(int, struct ioam6_trace_hdr *, __u32, __u16) = {
	[TEST_OUT_UNDEF_NS]		= check_ioam_header,
	[TEST_OUT_NO_ROOM]		= check_ioam_header,
	[TEST_OUT_BIT0]		= check_ioam_header_and_data,
	[TEST_OUT_BIT1]		= check_ioam_header_and_data,
	[TEST_OUT_BIT2]		= check_ioam_header_and_data,
	[TEST_OUT_BIT3]		= check_ioam_header_and_data,
	[TEST_OUT_BIT4]		= check_ioam_header_and_data,
	[TEST_OUT_BIT5]		= check_ioam_header_and_data,
	[TEST_OUT_BIT6]		= check_ioam_header_and_data,
	[TEST_OUT_BIT7]		= check_ioam_header_and_data,
	[TEST_OUT_BIT8]		= check_ioam_header_and_data,
	[TEST_OUT_BIT9]		= check_ioam_header_and_data,
	[TEST_OUT_BIT10]		= check_ioam_header_and_data,
	[TEST_OUT_BIT11]		= check_ioam_header_and_data,
	[TEST_OUT_BIT22]		= check_ioam_header_and_data,
	[TEST_OUT_FULL_SUPP_TRACE]	= check_ioam_header_and_data,
	[TEST_IN_UNDEF_NS]		= check_ioam_header,
	[TEST_IN_NO_ROOM]		= check_ioam_header,
	[TEST_IN_OFLAG]		= check_ioam_header,
	[TEST_IN_BIT0]			= check_ioam_header_and_data,
	[TEST_IN_BIT1]			= check_ioam_header_and_data,
	[TEST_IN_BIT2]			= check_ioam_header_and_data,
	[TEST_IN_BIT3]			= check_ioam_header_and_data,
	[TEST_IN_BIT4]			= check_ioam_header_and_data,
	[TEST_IN_BIT5]			= check_ioam_header_and_data,
	[TEST_IN_BIT6]			= check_ioam_header_and_data,
	[TEST_IN_BIT7]			= check_ioam_header_and_data,
	[TEST_IN_BIT8]			= check_ioam_header_and_data,
	[TEST_IN_BIT9]			= check_ioam_header_and_data,
	[TEST_IN_BIT10]		= check_ioam_header_and_data,
	[TEST_IN_BIT11]		= check_ioam_header_and_data,
	[TEST_IN_BIT22]		= check_ioam_header_and_data,
	[TEST_IN_FULL_SUPP_TRACE]	= check_ioam_header_and_data,
	[TEST_FWD_FULL_SUPP_TRACE]	= check_ioam_header_and_data,
};

int main(int argc, char **argv)
{
	int fd, size, hoplen, tid, ret = 1;
	struct in6_addr src, dst;
	struct ioam6_hdr *opt;
	struct ipv6hdr *ip6h;
	__u8 buffer[400], *p;
	__u16 ioam_ns;
	__u32 tr_type;

	if (argc != 7)
		goto out;

	tid = str2id(argv[2]);
	if (tid < 0 || !func[tid])
		goto out;

	if (inet_pton(AF_INET6, argv[3], &src) != 1 ||
	    inet_pton(AF_INET6, argv[4], &dst) != 1)
		goto out;

	if (get_u32(&tr_type, argv[5], 16) ||
	    get_u16(&ioam_ns, argv[6], 0))
		goto out;

	fd = socket(AF_PACKET, SOCK_DGRAM, __cpu_to_be16(ETH_P_IPV6));
	if (!fd)
		goto out;

	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
		       argv[1], strlen(argv[1])))
		goto close;

recv:
	size = recv(fd, buffer, sizeof(buffer), 0);
	if (size <= 0)
		goto close;

	ip6h = (struct ipv6hdr *)buffer;

	if (!ipv6_addr_equal(&ip6h->saddr, &src) ||
	    !ipv6_addr_equal(&ip6h->daddr, &dst))
		goto recv;

	if (ip6h->nexthdr != IPPROTO_HOPOPTS)
		goto close;

	p = buffer + sizeof(*ip6h);
	hoplen = (p[1] + 1) << 3;
	p += sizeof(struct ipv6_hopopt_hdr);

	while (hoplen > 0) {
		opt = (struct ioam6_hdr *)p;

		if (opt->opt_type == IPV6_TLV_IOAM &&
		    opt->type == IOAM6_TYPE_PREALLOC) {
			p += sizeof(*opt);
			ret = func[tid](tid, (struct ioam6_trace_hdr *)p,
					   tr_type, ioam_ns);
			break;
		}

		p += opt->opt_len + 2;
		hoplen -= opt->opt_len + 2;
	}
close:
	close(fd);
out:
	return ret;
}
