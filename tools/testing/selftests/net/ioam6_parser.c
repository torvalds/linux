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
#include <stdbool.h>
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
	.ns_data = 0xdeadbeef,
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
	.ns_data = 0xffffffff, /* default value */
	.ns_wide = 0xffffffffffffffff, /* default value */
	.sc_id = 0xffffff, /* default value */
	.sc_data = NULL,
	.hlim = 63,
};

enum {
	/**********
	 * OUTPUT *
	 **********/
	__TEST_OUT_MIN,

	TEST_OUT_UNDEF_NS,
	TEST_OUT_NO_ROOM,
	TEST_OUT_NO_ROOM_OSS,
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
	TEST_OUT_SIZE4,
	TEST_OUT_SIZE8,
	TEST_OUT_SIZE12,
	TEST_OUT_SIZE16,
	TEST_OUT_SIZE20,
	TEST_OUT_SIZE24,
	TEST_OUT_SIZE28,
	TEST_OUT_SIZE32,
	TEST_OUT_SIZE36,
	TEST_OUT_SIZE40,
	TEST_OUT_SIZE44,
	TEST_OUT_SIZE48,
	TEST_OUT_SIZE52,
	TEST_OUT_SIZE56,
	TEST_OUT_SIZE60,
	TEST_OUT_SIZE64,
	TEST_OUT_SIZE68,
	TEST_OUT_SIZE72,
	TEST_OUT_SIZE76,
	TEST_OUT_SIZE80,
	TEST_OUT_SIZE84,
	TEST_OUT_SIZE88,
	TEST_OUT_SIZE92,
	TEST_OUT_SIZE96,
	TEST_OUT_SIZE100,
	TEST_OUT_SIZE104,
	TEST_OUT_SIZE108,
	TEST_OUT_SIZE112,
	TEST_OUT_SIZE116,
	TEST_OUT_SIZE120,
	TEST_OUT_SIZE124,
	TEST_OUT_SIZE128,
	TEST_OUT_SIZE132,
	TEST_OUT_SIZE136,
	TEST_OUT_SIZE140,
	TEST_OUT_SIZE144,
	TEST_OUT_SIZE148,
	TEST_OUT_SIZE152,
	TEST_OUT_SIZE156,
	TEST_OUT_SIZE160,
	TEST_OUT_SIZE164,
	TEST_OUT_SIZE168,
	TEST_OUT_SIZE172,
	TEST_OUT_SIZE176,
	TEST_OUT_SIZE180,
	TEST_OUT_SIZE184,
	TEST_OUT_SIZE188,
	TEST_OUT_SIZE192,
	TEST_OUT_SIZE196,
	TEST_OUT_SIZE200,
	TEST_OUT_SIZE204,
	TEST_OUT_SIZE208,
	TEST_OUT_SIZE212,
	TEST_OUT_SIZE216,
	TEST_OUT_SIZE220,
	TEST_OUT_SIZE224,
	TEST_OUT_SIZE228,
	TEST_OUT_SIZE232,
	TEST_OUT_SIZE236,
	TEST_OUT_SIZE240,
	TEST_OUT_SIZE244,
	TEST_OUT_FULL_SUPP_TRACE,

	__TEST_OUT_MAX,

	/*********
	 * INPUT *
	 *********/
	__TEST_IN_MIN,

	TEST_IN_UNDEF_NS,
	TEST_IN_NO_ROOM,
	TEST_IN_NO_ROOM_OSS,
	TEST_IN_DISABLED,
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
	TEST_IN_SIZE4,
	TEST_IN_SIZE8,
	TEST_IN_SIZE12,
	TEST_IN_SIZE16,
	TEST_IN_SIZE20,
	TEST_IN_SIZE24,
	TEST_IN_SIZE28,
	TEST_IN_SIZE32,
	TEST_IN_SIZE36,
	TEST_IN_SIZE40,
	TEST_IN_SIZE44,
	TEST_IN_SIZE48,
	TEST_IN_SIZE52,
	TEST_IN_SIZE56,
	TEST_IN_SIZE60,
	TEST_IN_SIZE64,
	TEST_IN_SIZE68,
	TEST_IN_SIZE72,
	TEST_IN_SIZE76,
	TEST_IN_SIZE80,
	TEST_IN_SIZE84,
	TEST_IN_SIZE88,
	TEST_IN_SIZE92,
	TEST_IN_SIZE96,
	TEST_IN_SIZE100,
	TEST_IN_SIZE104,
	TEST_IN_SIZE108,
	TEST_IN_SIZE112,
	TEST_IN_SIZE116,
	TEST_IN_SIZE120,
	TEST_IN_SIZE124,
	TEST_IN_SIZE128,
	TEST_IN_SIZE132,
	TEST_IN_SIZE136,
	TEST_IN_SIZE140,
	TEST_IN_SIZE144,
	TEST_IN_SIZE148,
	TEST_IN_SIZE152,
	TEST_IN_SIZE156,
	TEST_IN_SIZE160,
	TEST_IN_SIZE164,
	TEST_IN_SIZE168,
	TEST_IN_SIZE172,
	TEST_IN_SIZE176,
	TEST_IN_SIZE180,
	TEST_IN_SIZE184,
	TEST_IN_SIZE188,
	TEST_IN_SIZE192,
	TEST_IN_SIZE196,
	TEST_IN_SIZE200,
	TEST_IN_SIZE204,
	TEST_IN_SIZE208,
	TEST_IN_SIZE212,
	TEST_IN_SIZE216,
	TEST_IN_SIZE220,
	TEST_IN_SIZE224,
	TEST_IN_SIZE228,
	TEST_IN_SIZE232,
	TEST_IN_SIZE236,
	TEST_IN_SIZE240,
	TEST_IN_SIZE244,
	TEST_IN_FULL_SUPP_TRACE,

	__TEST_IN_MAX,

	__TEST_MAX,
};

static int check_header(int tid, struct ioam6_trace_hdr *trace,
			__u32 trace_type, __u8 trace_size, __u16 ioam_ns)
{
	if (__be16_to_cpu(trace->namespace_id) != ioam_ns ||
	    __be32_to_cpu(trace->type_be32) != (trace_type << 8))
		return 1;

	switch (tid) {
	case TEST_OUT_UNDEF_NS:
	case TEST_IN_UNDEF_NS:
	case TEST_IN_DISABLED:
		return trace->overflow == 1 ||
		       trace->nodelen != 1 ||
		       trace->remlen != 1;

	case TEST_OUT_NO_ROOM:
	case TEST_IN_NO_ROOM:
	case TEST_IN_OFLAG:
		return trace->overflow == 0 ||
		       trace->nodelen != 2 ||
		       trace->remlen != 1;

	case TEST_OUT_NO_ROOM_OSS:
		return trace->overflow == 0 ||
		       trace->nodelen != 0 ||
		       trace->remlen != 1;

	case TEST_IN_NO_ROOM_OSS:
	case TEST_OUT_BIT22:
	case TEST_IN_BIT22:
		return trace->overflow == 1 ||
		       trace->nodelen != 0 ||
		       trace->remlen != 0;

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
		return trace->overflow == 1 ||
		       trace->nodelen != 1 ||
		       trace->remlen != 0;

	case TEST_OUT_BIT8:
	case TEST_IN_BIT8:
	case TEST_OUT_BIT9:
	case TEST_IN_BIT9:
	case TEST_OUT_BIT10:
	case TEST_IN_BIT10:
		return trace->overflow == 1 ||
		       trace->nodelen != 2 ||
		       trace->remlen != 0;

	case TEST_OUT_SIZE4:
	case TEST_OUT_SIZE8:
	case TEST_OUT_SIZE12:
	case TEST_OUT_SIZE16:
	case TEST_OUT_SIZE20:
	case TEST_OUT_SIZE24:
	case TEST_OUT_SIZE28:
	case TEST_OUT_SIZE32:
	case TEST_OUT_SIZE36:
	case TEST_OUT_SIZE40:
	case TEST_OUT_SIZE44:
	case TEST_OUT_SIZE48:
	case TEST_OUT_SIZE52:
	case TEST_OUT_SIZE56:
	case TEST_OUT_SIZE60:
	case TEST_OUT_SIZE64:
	case TEST_OUT_SIZE68:
	case TEST_OUT_SIZE72:
	case TEST_OUT_SIZE76:
	case TEST_OUT_SIZE80:
	case TEST_OUT_SIZE84:
	case TEST_OUT_SIZE88:
	case TEST_OUT_SIZE92:
	case TEST_OUT_SIZE96:
	case TEST_OUT_SIZE100:
	case TEST_OUT_SIZE104:
	case TEST_OUT_SIZE108:
	case TEST_OUT_SIZE112:
	case TEST_OUT_SIZE116:
	case TEST_OUT_SIZE120:
	case TEST_OUT_SIZE124:
	case TEST_OUT_SIZE128:
	case TEST_OUT_SIZE132:
	case TEST_OUT_SIZE136:
	case TEST_OUT_SIZE140:
	case TEST_OUT_SIZE144:
	case TEST_OUT_SIZE148:
	case TEST_OUT_SIZE152:
	case TEST_OUT_SIZE156:
	case TEST_OUT_SIZE160:
	case TEST_OUT_SIZE164:
	case TEST_OUT_SIZE168:
	case TEST_OUT_SIZE172:
	case TEST_OUT_SIZE176:
	case TEST_OUT_SIZE180:
	case TEST_OUT_SIZE184:
	case TEST_OUT_SIZE188:
	case TEST_OUT_SIZE192:
	case TEST_OUT_SIZE196:
	case TEST_OUT_SIZE200:
	case TEST_OUT_SIZE204:
	case TEST_OUT_SIZE208:
	case TEST_OUT_SIZE212:
	case TEST_OUT_SIZE216:
	case TEST_OUT_SIZE220:
	case TEST_OUT_SIZE224:
	case TEST_OUT_SIZE228:
	case TEST_OUT_SIZE232:
	case TEST_OUT_SIZE236:
	case TEST_OUT_SIZE240:
	case TEST_OUT_SIZE244:
		return trace->overflow == 1 ||
		       trace->nodelen != 1 ||
		       trace->remlen != trace_size / 4;

	case TEST_IN_SIZE4:
	case TEST_IN_SIZE8:
	case TEST_IN_SIZE12:
	case TEST_IN_SIZE16:
	case TEST_IN_SIZE20:
	case TEST_IN_SIZE24:
	case TEST_IN_SIZE28:
	case TEST_IN_SIZE32:
	case TEST_IN_SIZE36:
	case TEST_IN_SIZE40:
	case TEST_IN_SIZE44:
	case TEST_IN_SIZE48:
	case TEST_IN_SIZE52:
	case TEST_IN_SIZE56:
	case TEST_IN_SIZE60:
	case TEST_IN_SIZE64:
	case TEST_IN_SIZE68:
	case TEST_IN_SIZE72:
	case TEST_IN_SIZE76:
	case TEST_IN_SIZE80:
	case TEST_IN_SIZE84:
	case TEST_IN_SIZE88:
	case TEST_IN_SIZE92:
	case TEST_IN_SIZE96:
	case TEST_IN_SIZE100:
	case TEST_IN_SIZE104:
	case TEST_IN_SIZE108:
	case TEST_IN_SIZE112:
	case TEST_IN_SIZE116:
	case TEST_IN_SIZE120:
	case TEST_IN_SIZE124:
	case TEST_IN_SIZE128:
	case TEST_IN_SIZE132:
	case TEST_IN_SIZE136:
	case TEST_IN_SIZE140:
	case TEST_IN_SIZE144:
	case TEST_IN_SIZE148:
	case TEST_IN_SIZE152:
	case TEST_IN_SIZE156:
	case TEST_IN_SIZE160:
	case TEST_IN_SIZE164:
	case TEST_IN_SIZE168:
	case TEST_IN_SIZE172:
	case TEST_IN_SIZE176:
	case TEST_IN_SIZE180:
	case TEST_IN_SIZE184:
	case TEST_IN_SIZE188:
	case TEST_IN_SIZE192:
	case TEST_IN_SIZE196:
	case TEST_IN_SIZE200:
	case TEST_IN_SIZE204:
	case TEST_IN_SIZE208:
	case TEST_IN_SIZE212:
	case TEST_IN_SIZE216:
	case TEST_IN_SIZE220:
	case TEST_IN_SIZE224:
	case TEST_IN_SIZE228:
	case TEST_IN_SIZE232:
	case TEST_IN_SIZE236:
	case TEST_IN_SIZE240:
	case TEST_IN_SIZE244:
		return trace->overflow == 1 ||
		       trace->nodelen != 1 ||
		       trace->remlen != (trace_size / 4) - trace->nodelen;

	case TEST_OUT_FULL_SUPP_TRACE:
	case TEST_IN_FULL_SUPP_TRACE:
		return trace->overflow == 1 ||
		       trace->nodelen != 15 ||
		       trace->remlen != 0;

	default:
		break;
	}

	return 1;
}

static int check_data(struct ioam6_trace_hdr *trace, __u8 trace_size,
		      const struct ioam_config cnf, bool is_output)
{
	unsigned int len, i;
	__u8 aligned;
	__u64 raw64;
	__u32 raw32;
	__u8 *p;

	if (trace->type.bit12 | trace->type.bit13 | trace->type.bit14 |
	    trace->type.bit15 | trace->type.bit16 | trace->type.bit17 |
	    trace->type.bit18 | trace->type.bit19 | trace->type.bit20 |
	    trace->type.bit21 | trace->type.bit23)
		return 1;

	for (i = 0; i < trace->remlen * 4; i++) {
		if (trace->data[i] != 0)
			return 1;
	}

	if (trace->remlen * 4 == trace_size)
		return 0;

	p = trace->data + trace->remlen * 4;

	if (trace->type.bit0) {
		raw32 = __be32_to_cpu(*((__u32 *)p));
		if (cnf.hlim != (raw32 >> 24) || cnf.id != (raw32 & 0xffffff))
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit1) {
		raw32 = __be32_to_cpu(*((__u32 *)p));
		if (cnf.ingr_id != (raw32 >> 16) ||
		    cnf.egr_id != (raw32 & 0xffff))
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit2) {
		raw32 = __be32_to_cpu(*((__u32 *)p));
		if ((is_output && raw32 != 0xffffffff) ||
		    (!is_output && (raw32 == 0 || raw32 == 0xffffffff)))
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit3) {
		raw32 = __be32_to_cpu(*((__u32 *)p));
		if ((is_output && raw32 != 0xffffffff) ||
		    (!is_output && (raw32 == 0 || raw32 == 0xffffffff)))
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit4) {
		if (__be32_to_cpu(*((__u32 *)p)) != 0xffffffff)
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit5) {
		if (__be32_to_cpu(*((__u32 *)p)) != cnf.ns_data)
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit6) {
		if (__be32_to_cpu(*((__u32 *)p)) == 0xffffffff)
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit7) {
		if (__be32_to_cpu(*((__u32 *)p)) != 0xffffffff)
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit8) {
		raw64 = __be64_to_cpu(*((__u64 *)p));
		if (cnf.hlim != (raw64 >> 56) ||
		    cnf.wide != (raw64 & 0xffffffffffffff))
			return 1;
		p += sizeof(__u64);
	}

	if (trace->type.bit9) {
		if (__be32_to_cpu(*((__u32 *)p)) != cnf.ingr_wide)
			return 1;
		p += sizeof(__u32);

		if (__be32_to_cpu(*((__u32 *)p)) != cnf.egr_wide)
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit10) {
		if (__be64_to_cpu(*((__u64 *)p)) != cnf.ns_wide)
			return 1;
		p += sizeof(__u64);
	}

	if (trace->type.bit11) {
		if (__be32_to_cpu(*((__u32 *)p)) != 0xffffffff)
			return 1;
		p += sizeof(__u32);
	}

	if (trace->type.bit22) {
		len = cnf.sc_data ? strlen(cnf.sc_data) : 0;
		aligned = cnf.sc_data ? __ALIGN_KERNEL(len, 4) : 0;

		raw32 = __be32_to_cpu(*((__u32 *)p));
		if (aligned != (raw32 >> 24) * 4 ||
		    cnf.sc_id != (raw32 & 0xffffff))
			return 1;
		p += sizeof(__u32);

		if (cnf.sc_data) {
			if (strncmp((char *)p, cnf.sc_data, len))
				return 1;

			p += len;
			aligned -= len;

			while (aligned--) {
				if (*p != '\0')
					return 1;
				p += sizeof(__u8);
			}
		}
	}

	return 0;
}

static int check_ioam_trace(int tid, struct ioam6_trace_hdr *trace,
			    __u32 trace_type, __u8 trace_size, __u16 ioam_ns)
{
	if (check_header(tid, trace, trace_type, trace_size, ioam_ns))
		return 1;

	if (tid > __TEST_OUT_MIN && tid < __TEST_OUT_MAX)
		return check_data(trace, trace_size, node1, true);

	if (tid > __TEST_IN_MIN && tid < __TEST_IN_MAX)
		return check_data(trace, trace_size, node2, false);

	return 1;
}

static int str2id(const char *tname)
{
	if (!strcmp("output_undef_ns", tname))
		return TEST_OUT_UNDEF_NS;
	if (!strcmp("output_no_room", tname))
		return TEST_OUT_NO_ROOM;
	if (!strcmp("output_no_room_oss", tname))
		return TEST_OUT_NO_ROOM_OSS;
	if (!strcmp("output_bit0", tname))
		return TEST_OUT_BIT0;
	if (!strcmp("output_bit1", tname))
		return TEST_OUT_BIT1;
	if (!strcmp("output_bit2", tname))
		return TEST_OUT_BIT2;
	if (!strcmp("output_bit3", tname))
		return TEST_OUT_BIT3;
	if (!strcmp("output_bit4", tname))
		return TEST_OUT_BIT4;
	if (!strcmp("output_bit5", tname))
		return TEST_OUT_BIT5;
	if (!strcmp("output_bit6", tname))
		return TEST_OUT_BIT6;
	if (!strcmp("output_bit7", tname))
		return TEST_OUT_BIT7;
	if (!strcmp("output_bit8", tname))
		return TEST_OUT_BIT8;
	if (!strcmp("output_bit9", tname))
		return TEST_OUT_BIT9;
	if (!strcmp("output_bit10", tname))
		return TEST_OUT_BIT10;
	if (!strcmp("output_bit11", tname))
		return TEST_OUT_BIT11;
	if (!strcmp("output_bit22", tname))
		return TEST_OUT_BIT22;
	if (!strcmp("output_size4", tname))
		return TEST_OUT_SIZE4;
	if (!strcmp("output_size8", tname))
		return TEST_OUT_SIZE8;
	if (!strcmp("output_size12", tname))
		return TEST_OUT_SIZE12;
	if (!strcmp("output_size16", tname))
		return TEST_OUT_SIZE16;
	if (!strcmp("output_size20", tname))
		return TEST_OUT_SIZE20;
	if (!strcmp("output_size24", tname))
		return TEST_OUT_SIZE24;
	if (!strcmp("output_size28", tname))
		return TEST_OUT_SIZE28;
	if (!strcmp("output_size32", tname))
		return TEST_OUT_SIZE32;
	if (!strcmp("output_size36", tname))
		return TEST_OUT_SIZE36;
	if (!strcmp("output_size40", tname))
		return TEST_OUT_SIZE40;
	if (!strcmp("output_size44", tname))
		return TEST_OUT_SIZE44;
	if (!strcmp("output_size48", tname))
		return TEST_OUT_SIZE48;
	if (!strcmp("output_size52", tname))
		return TEST_OUT_SIZE52;
	if (!strcmp("output_size56", tname))
		return TEST_OUT_SIZE56;
	if (!strcmp("output_size60", tname))
		return TEST_OUT_SIZE60;
	if (!strcmp("output_size64", tname))
		return TEST_OUT_SIZE64;
	if (!strcmp("output_size68", tname))
		return TEST_OUT_SIZE68;
	if (!strcmp("output_size72", tname))
		return TEST_OUT_SIZE72;
	if (!strcmp("output_size76", tname))
		return TEST_OUT_SIZE76;
	if (!strcmp("output_size80", tname))
		return TEST_OUT_SIZE80;
	if (!strcmp("output_size84", tname))
		return TEST_OUT_SIZE84;
	if (!strcmp("output_size88", tname))
		return TEST_OUT_SIZE88;
	if (!strcmp("output_size92", tname))
		return TEST_OUT_SIZE92;
	if (!strcmp("output_size96", tname))
		return TEST_OUT_SIZE96;
	if (!strcmp("output_size100", tname))
		return TEST_OUT_SIZE100;
	if (!strcmp("output_size104", tname))
		return TEST_OUT_SIZE104;
	if (!strcmp("output_size108", tname))
		return TEST_OUT_SIZE108;
	if (!strcmp("output_size112", tname))
		return TEST_OUT_SIZE112;
	if (!strcmp("output_size116", tname))
		return TEST_OUT_SIZE116;
	if (!strcmp("output_size120", tname))
		return TEST_OUT_SIZE120;
	if (!strcmp("output_size124", tname))
		return TEST_OUT_SIZE124;
	if (!strcmp("output_size128", tname))
		return TEST_OUT_SIZE128;
	if (!strcmp("output_size132", tname))
		return TEST_OUT_SIZE132;
	if (!strcmp("output_size136", tname))
		return TEST_OUT_SIZE136;
	if (!strcmp("output_size140", tname))
		return TEST_OUT_SIZE140;
	if (!strcmp("output_size144", tname))
		return TEST_OUT_SIZE144;
	if (!strcmp("output_size148", tname))
		return TEST_OUT_SIZE148;
	if (!strcmp("output_size152", tname))
		return TEST_OUT_SIZE152;
	if (!strcmp("output_size156", tname))
		return TEST_OUT_SIZE156;
	if (!strcmp("output_size160", tname))
		return TEST_OUT_SIZE160;
	if (!strcmp("output_size164", tname))
		return TEST_OUT_SIZE164;
	if (!strcmp("output_size168", tname))
		return TEST_OUT_SIZE168;
	if (!strcmp("output_size172", tname))
		return TEST_OUT_SIZE172;
	if (!strcmp("output_size176", tname))
		return TEST_OUT_SIZE176;
	if (!strcmp("output_size180", tname))
		return TEST_OUT_SIZE180;
	if (!strcmp("output_size184", tname))
		return TEST_OUT_SIZE184;
	if (!strcmp("output_size188", tname))
		return TEST_OUT_SIZE188;
	if (!strcmp("output_size192", tname))
		return TEST_OUT_SIZE192;
	if (!strcmp("output_size196", tname))
		return TEST_OUT_SIZE196;
	if (!strcmp("output_size200", tname))
		return TEST_OUT_SIZE200;
	if (!strcmp("output_size204", tname))
		return TEST_OUT_SIZE204;
	if (!strcmp("output_size208", tname))
		return TEST_OUT_SIZE208;
	if (!strcmp("output_size212", tname))
		return TEST_OUT_SIZE212;
	if (!strcmp("output_size216", tname))
		return TEST_OUT_SIZE216;
	if (!strcmp("output_size220", tname))
		return TEST_OUT_SIZE220;
	if (!strcmp("output_size224", tname))
		return TEST_OUT_SIZE224;
	if (!strcmp("output_size228", tname))
		return TEST_OUT_SIZE228;
	if (!strcmp("output_size232", tname))
		return TEST_OUT_SIZE232;
	if (!strcmp("output_size236", tname))
		return TEST_OUT_SIZE236;
	if (!strcmp("output_size240", tname))
		return TEST_OUT_SIZE240;
	if (!strcmp("output_size244", tname))
		return TEST_OUT_SIZE244;
	if (!strcmp("output_full_supp_trace", tname))
		return TEST_OUT_FULL_SUPP_TRACE;
	if (!strcmp("input_undef_ns", tname))
		return TEST_IN_UNDEF_NS;
	if (!strcmp("input_no_room", tname))
		return TEST_IN_NO_ROOM;
	if (!strcmp("input_no_room_oss", tname))
		return TEST_IN_NO_ROOM_OSS;
	if (!strcmp("input_disabled", tname))
		return TEST_IN_DISABLED;
	if (!strcmp("input_oflag", tname))
		return TEST_IN_OFLAG;
	if (!strcmp("input_bit0", tname))
		return TEST_IN_BIT0;
	if (!strcmp("input_bit1", tname))
		return TEST_IN_BIT1;
	if (!strcmp("input_bit2", tname))
		return TEST_IN_BIT2;
	if (!strcmp("input_bit3", tname))
		return TEST_IN_BIT3;
	if (!strcmp("input_bit4", tname))
		return TEST_IN_BIT4;
	if (!strcmp("input_bit5", tname))
		return TEST_IN_BIT5;
	if (!strcmp("input_bit6", tname))
		return TEST_IN_BIT6;
	if (!strcmp("input_bit7", tname))
		return TEST_IN_BIT7;
	if (!strcmp("input_bit8", tname))
		return TEST_IN_BIT8;
	if (!strcmp("input_bit9", tname))
		return TEST_IN_BIT9;
	if (!strcmp("input_bit10", tname))
		return TEST_IN_BIT10;
	if (!strcmp("input_bit11", tname))
		return TEST_IN_BIT11;
	if (!strcmp("input_bit22", tname))
		return TEST_IN_BIT22;
	if (!strcmp("input_size4", tname))
		return TEST_IN_SIZE4;
	if (!strcmp("input_size8", tname))
		return TEST_IN_SIZE8;
	if (!strcmp("input_size12", tname))
		return TEST_IN_SIZE12;
	if (!strcmp("input_size16", tname))
		return TEST_IN_SIZE16;
	if (!strcmp("input_size20", tname))
		return TEST_IN_SIZE20;
	if (!strcmp("input_size24", tname))
		return TEST_IN_SIZE24;
	if (!strcmp("input_size28", tname))
		return TEST_IN_SIZE28;
	if (!strcmp("input_size32", tname))
		return TEST_IN_SIZE32;
	if (!strcmp("input_size36", tname))
		return TEST_IN_SIZE36;
	if (!strcmp("input_size40", tname))
		return TEST_IN_SIZE40;
	if (!strcmp("input_size44", tname))
		return TEST_IN_SIZE44;
	if (!strcmp("input_size48", tname))
		return TEST_IN_SIZE48;
	if (!strcmp("input_size52", tname))
		return TEST_IN_SIZE52;
	if (!strcmp("input_size56", tname))
		return TEST_IN_SIZE56;
	if (!strcmp("input_size60", tname))
		return TEST_IN_SIZE60;
	if (!strcmp("input_size64", tname))
		return TEST_IN_SIZE64;
	if (!strcmp("input_size68", tname))
		return TEST_IN_SIZE68;
	if (!strcmp("input_size72", tname))
		return TEST_IN_SIZE72;
	if (!strcmp("input_size76", tname))
		return TEST_IN_SIZE76;
	if (!strcmp("input_size80", tname))
		return TEST_IN_SIZE80;
	if (!strcmp("input_size84", tname))
		return TEST_IN_SIZE84;
	if (!strcmp("input_size88", tname))
		return TEST_IN_SIZE88;
	if (!strcmp("input_size92", tname))
		return TEST_IN_SIZE92;
	if (!strcmp("input_size96", tname))
		return TEST_IN_SIZE96;
	if (!strcmp("input_size100", tname))
		return TEST_IN_SIZE100;
	if (!strcmp("input_size104", tname))
		return TEST_IN_SIZE104;
	if (!strcmp("input_size108", tname))
		return TEST_IN_SIZE108;
	if (!strcmp("input_size112", tname))
		return TEST_IN_SIZE112;
	if (!strcmp("input_size116", tname))
		return TEST_IN_SIZE116;
	if (!strcmp("input_size120", tname))
		return TEST_IN_SIZE120;
	if (!strcmp("input_size124", tname))
		return TEST_IN_SIZE124;
	if (!strcmp("input_size128", tname))
		return TEST_IN_SIZE128;
	if (!strcmp("input_size132", tname))
		return TEST_IN_SIZE132;
	if (!strcmp("input_size136", tname))
		return TEST_IN_SIZE136;
	if (!strcmp("input_size140", tname))
		return TEST_IN_SIZE140;
	if (!strcmp("input_size144", tname))
		return TEST_IN_SIZE144;
	if (!strcmp("input_size148", tname))
		return TEST_IN_SIZE148;
	if (!strcmp("input_size152", tname))
		return TEST_IN_SIZE152;
	if (!strcmp("input_size156", tname))
		return TEST_IN_SIZE156;
	if (!strcmp("input_size160", tname))
		return TEST_IN_SIZE160;
	if (!strcmp("input_size164", tname))
		return TEST_IN_SIZE164;
	if (!strcmp("input_size168", tname))
		return TEST_IN_SIZE168;
	if (!strcmp("input_size172", tname))
		return TEST_IN_SIZE172;
	if (!strcmp("input_size176", tname))
		return TEST_IN_SIZE176;
	if (!strcmp("input_size180", tname))
		return TEST_IN_SIZE180;
	if (!strcmp("input_size184", tname))
		return TEST_IN_SIZE184;
	if (!strcmp("input_size188", tname))
		return TEST_IN_SIZE188;
	if (!strcmp("input_size192", tname))
		return TEST_IN_SIZE192;
	if (!strcmp("input_size196", tname))
		return TEST_IN_SIZE196;
	if (!strcmp("input_size200", tname))
		return TEST_IN_SIZE200;
	if (!strcmp("input_size204", tname))
		return TEST_IN_SIZE204;
	if (!strcmp("input_size208", tname))
		return TEST_IN_SIZE208;
	if (!strcmp("input_size212", tname))
		return TEST_IN_SIZE212;
	if (!strcmp("input_size216", tname))
		return TEST_IN_SIZE216;
	if (!strcmp("input_size220", tname))
		return TEST_IN_SIZE220;
	if (!strcmp("input_size224", tname))
		return TEST_IN_SIZE224;
	if (!strcmp("input_size228", tname))
		return TEST_IN_SIZE228;
	if (!strcmp("input_size232", tname))
		return TEST_IN_SIZE232;
	if (!strcmp("input_size236", tname))
		return TEST_IN_SIZE236;
	if (!strcmp("input_size240", tname))
		return TEST_IN_SIZE240;
	if (!strcmp("input_size244", tname))
		return TEST_IN_SIZE244;
	if (!strcmp("input_full_supp_trace", tname))
		return TEST_IN_FULL_SUPP_TRACE;

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

static int get_u8(__u8 *val, const char *arg, int base)
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

	if (res > 0xFFUL)
		return -1;

	*val = res;
	return 0;
}

int main(int argc, char **argv)
{
	__u8 buffer[512], *ptr, nexthdr, tr_size;
	struct ioam6_trace_hdr *trace;
	unsigned int hoplen, ret = 1;
	struct ipv6_hopopt_hdr *hbh;
	int fd, size, testname_id;
	struct in6_addr src, dst;
	struct ioam6_hdr *ioam6;
	struct timeval timeout;
	struct ipv6hdr *ipv6;
	__u32 tr_type;
	__u16 ioam_ns;

	if (argc != 9)
		goto out;

	testname_id = str2id(argv[2]);

	if (testname_id < 0 ||
	    inet_pton(AF_INET6, argv[3], &src) != 1 ||
	    inet_pton(AF_INET6, argv[4], &dst) != 1 ||
	    get_u32(&tr_type, argv[5], 16) ||
	    get_u8(&tr_size, argv[6], 0) ||
	    get_u16(&ioam_ns, argv[7], 0))
		goto out;

	nexthdr = (!strcmp(argv[8], "encap") ? IPPROTO_IPV6 : IPPROTO_ICMPV6);

	hoplen = sizeof(*hbh);
	hoplen += 2; // 2-byte padding for alignment
	hoplen += sizeof(*ioam6); // IOAM option header
	hoplen += sizeof(*trace); // IOAM trace header
	hoplen += tr_size; // IOAM trace size
	hoplen += (tr_size % 8); // optional padding

	fd = socket(AF_PACKET, SOCK_DGRAM, __cpu_to_be16(ETH_P_IPV6));
	if (fd < 0)
		goto out;

	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
		       argv[1], strlen(argv[1])))
		goto close;

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
		       (const char *)&timeout, sizeof(timeout)))
		goto close;
recv:
	size = recv(fd, buffer, sizeof(buffer), 0);
	if (size <= 0)
		goto close;

	ipv6 = (struct ipv6hdr *)buffer;

	/* Skip packets that do not have the expected src/dst address or that
	 * do not have a Hop-by-hop.
	 */
	if (!ipv6_addr_equal(&ipv6->saddr, &src) ||
	    !ipv6_addr_equal(&ipv6->daddr, &dst) ||
	    ipv6->nexthdr != IPPROTO_HOPOPTS)
		goto recv;

	/* Check Hbh's Next Header and Size. */
	hbh = (struct ipv6_hopopt_hdr *)(buffer + sizeof(*ipv6));
	if (hbh->nexthdr != nexthdr || hbh->hdrlen != (hoplen >> 3) - 1)
		goto close;

	/* Check we have a 2-byte padding for alignment. */
	ptr = (__u8 *)hbh + sizeof(*hbh);
	if (ptr[0] != IPV6_TLV_PADN && ptr[1] != 0)
		goto close;

	/* Check we now have the IOAM option. */
	ptr += 2;
	if (ptr[0] != IPV6_TLV_IOAM)
		goto close;

	/* Check its size and the IOAM option type. */
	ioam6 = (struct ioam6_hdr *)ptr;
	if (ioam6->opt_len != sizeof(*ioam6) - 2 + sizeof(*trace) + tr_size ||
	    ioam6->type != IOAM6_TYPE_PREALLOC)
		goto close;

	trace = (struct ioam6_trace_hdr *)(ptr + sizeof(*ioam6));

	/* Check the trailing 4-byte padding (potentially). */
	ptr = (__u8 *)trace + sizeof(*trace) + tr_size;
	if (tr_size % 8 && ptr[0] != IPV6_TLV_PADN && ptr[1] != 2 &&
	    ptr[2] != 0 && ptr[3] != 0)
		goto close;

	/* Check the IOAM header and data. */
	ret = check_ioam_trace(testname_id, trace, tr_type, tr_size, ioam_ns);
close:
	close(fd);
out:
	return ret;
}
