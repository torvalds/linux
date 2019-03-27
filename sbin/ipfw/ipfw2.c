/*-
 * Copyright (c) 2002-2003 Luigi Rizzo
 * Copyright (c) 1996 Alex Nash, Paul Traina, Poul-Henning Kamp
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Idea and grammar partially left from:
 * Copyright (c) 1993 Daniel Boulet
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 *
 * NEW command line interface for IP firewall facility
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include "ipfw2.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <jail.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>	/* ctime */
#include <timeconv.h>	/* _long_to_time */
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>	/* offsetof */

#include <net/ethernet.h>
#include <net/if.h>		/* only IFNAMSIZ */
#include <netinet/in.h>
#include <netinet/in_systm.h>	/* only n_short, n_long */
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_fw.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

struct cmdline_opts co;	/* global options */

struct format_opts {
	int bcwidth;
	int pcwidth;
	int show_counters;
	int show_time;		/* show timestamp */
	uint32_t set_mask;	/* enabled sets mask */
	uint32_t flags;		/* request flags */
	uint32_t first;		/* first rule to request */
	uint32_t last;		/* last rule to request */
	uint32_t dcnt;		/* number of dynamic states */
	ipfw_obj_ctlv *tstate;	/* table state data */
};

int resvd_set_number = RESVD_SET;

int ipfw_socket = -1;

#define	CHECK_LENGTH(v, len) do {				\
	if ((v) < (len))					\
		errx(EX_DATAERR, "Rule too long");		\
	} while (0)
/*
 * Check if we have enough space in cmd buffer. Note that since
 * first 8? u32 words are reserved by reserved header, full cmd
 * buffer can't be used, so we need to protect from buffer overrun
 * only. At the beginning, cblen is less than actual buffer size by
 * size of ipfw_insn_u32 instruction + 1 u32 work. This eliminates need
 * for checking small instructions fitting in given range.
 * We also (ab)use the fact that ipfw_insn is always the first field
 * for any custom instruction.
 */
#define	CHECK_CMDLEN	CHECK_LENGTH(cblen, F_LEN((ipfw_insn *)cmd))

#define GET_UINT_ARG(arg, min, max, tok, s_x) do {			\
	if (!av[0])							\
		errx(EX_USAGE, "%s: missing argument", match_value(s_x, tok)); \
	if (_substrcmp(*av, "tablearg") == 0) {				\
		arg = IP_FW_TARG;					\
		break;							\
	}								\
									\
	{								\
	long _xval;							\
	char *end;							\
									\
	_xval = strtol(*av, &end, 10);					\
									\
	if (!isdigit(**av) || *end != '\0' || (_xval == 0 && errno == EINVAL)) \
		errx(EX_DATAERR, "%s: invalid argument: %s",		\
		    match_value(s_x, tok), *av);			\
									\
	if (errno == ERANGE || _xval < min || _xval > max)		\
		errx(EX_DATAERR, "%s: argument is out of range (%u..%u): %s", \
		    match_value(s_x, tok), min, max, *av);		\
									\
	if (_xval == IP_FW_TARG)					\
		errx(EX_DATAERR, "%s: illegal argument value: %s",	\
		    match_value(s_x, tok), *av);			\
	arg = _xval;							\
	}								\
} while (0)

static struct _s_x f_tcpflags[] = {
	{ "syn", TH_SYN },
	{ "fin", TH_FIN },
	{ "ack", TH_ACK },
	{ "psh", TH_PUSH },
	{ "rst", TH_RST },
	{ "urg", TH_URG },
	{ "tcp flag", 0 },
	{ NULL,	0 }
};

static struct _s_x f_tcpopts[] = {
	{ "mss",	IP_FW_TCPOPT_MSS },
	{ "maxseg",	IP_FW_TCPOPT_MSS },
	{ "window",	IP_FW_TCPOPT_WINDOW },
	{ "sack",	IP_FW_TCPOPT_SACK },
	{ "ts",		IP_FW_TCPOPT_TS },
	{ "timestamp",	IP_FW_TCPOPT_TS },
	{ "cc",		IP_FW_TCPOPT_CC },
	{ "tcp option",	0 },
	{ NULL,	0 }
};

/*
 * IP options span the range 0 to 255 so we need to remap them
 * (though in fact only the low 5 bits are significant).
 */
static struct _s_x f_ipopts[] = {
	{ "ssrr",	IP_FW_IPOPT_SSRR},
	{ "lsrr",	IP_FW_IPOPT_LSRR},
	{ "rr",		IP_FW_IPOPT_RR},
	{ "ts",		IP_FW_IPOPT_TS},
	{ "ip option",	0 },
	{ NULL,	0 }
};

static struct _s_x f_iptos[] = {
	{ "lowdelay",	IPTOS_LOWDELAY},
	{ "throughput",	IPTOS_THROUGHPUT},
	{ "reliability", IPTOS_RELIABILITY},
	{ "mincost",	IPTOS_MINCOST},
	{ "congestion",	IPTOS_ECN_CE},
	{ "ecntransport", IPTOS_ECN_ECT0},
	{ "ip tos option", 0},
	{ NULL,	0 }
};

struct _s_x f_ipdscp[] = {
	{ "af11", IPTOS_DSCP_AF11 >> 2 },	/* 001010 */
	{ "af12", IPTOS_DSCP_AF12 >> 2 },	/* 001100 */
	{ "af13", IPTOS_DSCP_AF13 >> 2 },	/* 001110 */
	{ "af21", IPTOS_DSCP_AF21 >> 2 },	/* 010010 */
	{ "af22", IPTOS_DSCP_AF22 >> 2 },	/* 010100 */
	{ "af23", IPTOS_DSCP_AF23 >> 2 },	/* 010110 */
	{ "af31", IPTOS_DSCP_AF31 >> 2 },	/* 011010 */
	{ "af32", IPTOS_DSCP_AF32 >> 2 },	/* 011100 */
	{ "af33", IPTOS_DSCP_AF33 >> 2 },	/* 011110 */
	{ "af41", IPTOS_DSCP_AF41 >> 2 },	/* 100010 */
	{ "af42", IPTOS_DSCP_AF42 >> 2 },	/* 100100 */
	{ "af43", IPTOS_DSCP_AF43 >> 2 },	/* 100110 */
	{ "be", IPTOS_DSCP_CS0 >> 2 }, 	/* 000000 */
	{ "ef", IPTOS_DSCP_EF >> 2 },	/* 101110 */
	{ "cs0", IPTOS_DSCP_CS0 >> 2 },	/* 000000 */
	{ "cs1", IPTOS_DSCP_CS1 >> 2 },	/* 001000 */
	{ "cs2", IPTOS_DSCP_CS2 >> 2 },	/* 010000 */
	{ "cs3", IPTOS_DSCP_CS3 >> 2 },	/* 011000 */
	{ "cs4", IPTOS_DSCP_CS4 >> 2 },	/* 100000 */
	{ "cs5", IPTOS_DSCP_CS5 >> 2 },	/* 101000 */
	{ "cs6", IPTOS_DSCP_CS6 >> 2 },	/* 110000 */
	{ "cs7", IPTOS_DSCP_CS7 >> 2 },	/* 100000 */
	{ NULL, 0 }
};

static struct _s_x limit_masks[] = {
	{"all",		DYN_SRC_ADDR|DYN_SRC_PORT|DYN_DST_ADDR|DYN_DST_PORT},
	{"src-addr",	DYN_SRC_ADDR},
	{"src-port",	DYN_SRC_PORT},
	{"dst-addr",	DYN_DST_ADDR},
	{"dst-port",	DYN_DST_PORT},
	{NULL,		0}
};

/*
 * we use IPPROTO_ETHERTYPE as a fake protocol id to call the print routines
 * This is only used in this code.
 */
#define IPPROTO_ETHERTYPE	0x1000
static struct _s_x ether_types[] = {
    /*
     * Note, we cannot use "-:&/" in the names because they are field
     * separators in the type specifications. Also, we use s = NULL as
     * end-delimiter, because a type of 0 can be legal.
     */
	{ "ip",		0x0800 },
	{ "ipv4",	0x0800 },
	{ "ipv6",	0x86dd },
	{ "arp",	0x0806 },
	{ "rarp",	0x8035 },
	{ "vlan",	0x8100 },
	{ "loop",	0x9000 },
	{ "trail",	0x1000 },
	{ "at",		0x809b },
	{ "atalk",	0x809b },
	{ "aarp",	0x80f3 },
	{ "pppoe_disc",	0x8863 },
	{ "pppoe_sess",	0x8864 },
	{ "ipx_8022",	0x00E0 },
	{ "ipx_8023",	0x0000 },
	{ "ipx_ii",	0x8137 },
	{ "ipx_snap",	0x8137 },
	{ "ipx",	0x8137 },
	{ "ns",		0x0600 },
	{ NULL,		0 }
};

static struct _s_x rule_eactions[] = {
	{ "nat64clat",		TOK_NAT64CLAT },
	{ "nat64lsn",		TOK_NAT64LSN },
	{ "nat64stl",		TOK_NAT64STL },
	{ "nptv6",		TOK_NPTV6 },
	{ "tcp-setmss",		TOK_TCPSETMSS },
	{ NULL, 0 }	/* terminator */
};

static struct _s_x rule_actions[] = {
	{ "abort6",		TOK_ABORT6 },
	{ "abort",		TOK_ABORT },
	{ "accept",		TOK_ACCEPT },
	{ "pass",		TOK_ACCEPT },
	{ "allow",		TOK_ACCEPT },
	{ "permit",		TOK_ACCEPT },
	{ "count",		TOK_COUNT },
	{ "pipe",		TOK_PIPE },
	{ "queue",		TOK_QUEUE },
	{ "divert",		TOK_DIVERT },
	{ "tee",		TOK_TEE },
	{ "netgraph",		TOK_NETGRAPH },
	{ "ngtee",		TOK_NGTEE },
	{ "fwd",		TOK_FORWARD },
	{ "forward",		TOK_FORWARD },
	{ "skipto",		TOK_SKIPTO },
	{ "deny",		TOK_DENY },
	{ "drop",		TOK_DENY },
	{ "reject",		TOK_REJECT },
	{ "reset6",		TOK_RESET6 },
	{ "reset",		TOK_RESET },
	{ "unreach6",		TOK_UNREACH6 },
	{ "unreach",		TOK_UNREACH },
	{ "check-state",	TOK_CHECKSTATE },
	{ "//",			TOK_COMMENT },
	{ "nat",		TOK_NAT },
	{ "reass",		TOK_REASS },
	{ "setfib",		TOK_SETFIB },
	{ "setdscp",		TOK_SETDSCP },
	{ "call",		TOK_CALL },
	{ "return",		TOK_RETURN },
	{ "eaction",		TOK_EACTION },
	{ "tcp-setmss",		TOK_TCPSETMSS },
	{ NULL, 0 }	/* terminator */
};

static struct _s_x rule_action_params[] = {
	{ "altq",		TOK_ALTQ },
	{ "log",		TOK_LOG },
	{ "tag",		TOK_TAG },
	{ "untag",		TOK_UNTAG },
	{ NULL, 0 }	/* terminator */
};

/*
 * The 'lookup' instruction accepts one of the following arguments.
 * -1 is a terminator for the list.
 * Arguments are passed as v[1] in O_DST_LOOKUP options.
 */
static int lookup_key[] = {
	TOK_DSTIP, TOK_SRCIP, TOK_DSTPORT, TOK_SRCPORT,
	TOK_UID, TOK_JAIL, TOK_DSCP, -1 };

static struct _s_x rule_options[] = {
	{ "tagged",		TOK_TAGGED },
	{ "uid",		TOK_UID },
	{ "gid",		TOK_GID },
	{ "jail",		TOK_JAIL },
	{ "in",			TOK_IN },
	{ "limit",		TOK_LIMIT },
	{ "set-limit",		TOK_SETLIMIT },
	{ "keep-state",		TOK_KEEPSTATE },
	{ "record-state",	TOK_RECORDSTATE },
	{ "bridged",		TOK_LAYER2 },
	{ "layer2",		TOK_LAYER2 },
	{ "out",		TOK_OUT },
	{ "diverted",		TOK_DIVERTED },
	{ "diverted-loopback",	TOK_DIVERTEDLOOPBACK },
	{ "diverted-output",	TOK_DIVERTEDOUTPUT },
	{ "xmit",		TOK_XMIT },
	{ "recv",		TOK_RECV },
	{ "via",		TOK_VIA },
	{ "fragment",		TOK_FRAG },
	{ "frag",		TOK_FRAG },
	{ "fib",		TOK_FIB },
	{ "ipoptions",		TOK_IPOPTS },
	{ "ipopts",		TOK_IPOPTS },
	{ "iplen",		TOK_IPLEN },
	{ "ipid",		TOK_IPID },
	{ "ipprecedence",	TOK_IPPRECEDENCE },
	{ "dscp",		TOK_DSCP },
	{ "iptos",		TOK_IPTOS },
	{ "ipttl",		TOK_IPTTL },
	{ "ipversion",		TOK_IPVER },
	{ "ipver",		TOK_IPVER },
	{ "estab",		TOK_ESTAB },
	{ "established",	TOK_ESTAB },
	{ "setup",		TOK_SETUP },
	{ "sockarg",		TOK_SOCKARG },
	{ "tcpdatalen",		TOK_TCPDATALEN },
	{ "tcpflags",		TOK_TCPFLAGS },
	{ "tcpflgs",		TOK_TCPFLAGS },
	{ "tcpoptions",		TOK_TCPOPTS },
	{ "tcpopts",		TOK_TCPOPTS },
	{ "tcpseq",		TOK_TCPSEQ },
	{ "tcpack",		TOK_TCPACK },
	{ "tcpwin",		TOK_TCPWIN },
	{ "icmptype",		TOK_ICMPTYPES },
	{ "icmptypes",		TOK_ICMPTYPES },
	{ "dst-ip",		TOK_DSTIP },
	{ "src-ip",		TOK_SRCIP },
	{ "dst-port",		TOK_DSTPORT },
	{ "src-port",		TOK_SRCPORT },
	{ "proto",		TOK_PROTO },
	{ "MAC",		TOK_MAC },
	{ "mac",		TOK_MAC },
	{ "mac-type",		TOK_MACTYPE },
	{ "verrevpath",		TOK_VERREVPATH },
	{ "versrcreach",	TOK_VERSRCREACH },
	{ "antispoof",		TOK_ANTISPOOF },
	{ "ipsec",		TOK_IPSEC },
	{ "icmp6type",		TOK_ICMP6TYPES },
	{ "icmp6types",		TOK_ICMP6TYPES },
	{ "ext6hdr",		TOK_EXT6HDR},
	{ "flow-id",		TOK_FLOWID},
	{ "ipv6",		TOK_IPV6},
	{ "ip6",		TOK_IPV6},
	{ "ipv4",		TOK_IPV4},
	{ "ip4",		TOK_IPV4},
	{ "dst-ipv6",		TOK_DSTIP6},
	{ "dst-ip6",		TOK_DSTIP6},
	{ "src-ipv6",		TOK_SRCIP6},
	{ "src-ip6",		TOK_SRCIP6},
	{ "lookup",		TOK_LOOKUP},
	{ "flow",		TOK_FLOW},
	{ "defer-action",	TOK_SKIPACTION },
	{ "defer-immediate-action",	TOK_SKIPACTION },
	{ "//",			TOK_COMMENT },

	{ "not",		TOK_NOT },		/* pseudo option */
	{ "!", /* escape ? */	TOK_NOT },		/* pseudo option */
	{ "or",			TOK_OR },		/* pseudo option */
	{ "|", /* escape */	TOK_OR },		/* pseudo option */
	{ "{",			TOK_STARTBRACE },	/* pseudo option */
	{ "(",			TOK_STARTBRACE },	/* pseudo option */
	{ "}",			TOK_ENDBRACE },		/* pseudo option */
	{ ")",			TOK_ENDBRACE },		/* pseudo option */
	{ NULL, 0 }	/* terminator */
};

void bprint_uint_arg(struct buf_pr *bp, const char *str, uint32_t arg);
static int ipfw_get_config(struct cmdline_opts *co, struct format_opts *fo,
    ipfw_cfg_lheader **pcfg, size_t *psize);
static int ipfw_show_config(struct cmdline_opts *co, struct format_opts *fo,
    ipfw_cfg_lheader *cfg, size_t sz, int ac, char **av);
static void ipfw_list_tifaces(void);

struct tidx;
static uint16_t pack_object(struct tidx *tstate, char *name, int otype);
static uint16_t pack_table(struct tidx *tstate, char *name);

static char *table_search_ctlv(ipfw_obj_ctlv *ctlv, uint16_t idx);
static void object_sort_ctlv(ipfw_obj_ctlv *ctlv);
static char *object_search_ctlv(ipfw_obj_ctlv *ctlv, uint16_t idx,
    uint16_t type);

/*
 * Simple string buffer API.
 * Used to simplify buffer passing between function and for
 * transparent overrun handling.
 */

/*
 * Allocates new buffer of given size @sz.
 *
 * Returns 0 on success.
 */
int
bp_alloc(struct buf_pr *b, size_t size)
{
	memset(b, 0, sizeof(struct buf_pr));

	if ((b->buf = calloc(1, size)) == NULL)
		return (ENOMEM);

	b->ptr = b->buf;
	b->size = size;
	b->avail = b->size;

	return (0);
}

void
bp_free(struct buf_pr *b)
{

	free(b->buf);
}

/*
 * Flushes buffer so new writer start from beginning.
 */
void
bp_flush(struct buf_pr *b)
{

	b->ptr = b->buf;
	b->avail = b->size;
	b->buf[0] = '\0';
}

/*
 * Print message specified by @format and args.
 * Automatically manage buffer space and transparently handle
 * buffer overruns.
 *
 * Returns number of bytes that should have been printed.
 */
int
bprintf(struct buf_pr *b, char *format, ...)
{
	va_list args;
	int i;

	va_start(args, format);

	i = vsnprintf(b->ptr, b->avail, format, args);
	va_end(args);

	if (i > b->avail || i < 0) {
		/* Overflow or print error */
		b->avail = 0;
	} else {
		b->ptr += i;
		b->avail -= i;
	} 

	b->needed += i;

	return (i);
}

/*
 * Special values printer for tablearg-aware opcodes.
 */
void
bprint_uint_arg(struct buf_pr *bp, const char *str, uint32_t arg)
{

	if (str != NULL)
		bprintf(bp, "%s", str);
	if (arg == IP_FW_TARG)
		bprintf(bp, "tablearg");
	else
		bprintf(bp, "%u", arg);
}

/*
 * Helper routine to print a possibly unaligned uint64_t on
 * various platform. If width > 0, print the value with
 * the desired width, followed by a space;
 * otherwise, return the required width.
 */
int
pr_u64(struct buf_pr *b, uint64_t *pd, int width)
{
#ifdef TCC
#define U64_FMT "I64"
#else
#define U64_FMT "llu"
#endif
	uint64_t u;
	unsigned long long d;

	bcopy (pd, &u, sizeof(u));
	d = u;
	return (width > 0) ?
		bprintf(b, "%*" U64_FMT " ", width, d) :
		snprintf(NULL, 0, "%" U64_FMT, d) ;
#undef U64_FMT
}


void *
safe_calloc(size_t number, size_t size)
{
	void *ret = calloc(number, size);

	if (ret == NULL)
		err(EX_OSERR, "calloc");
	return ret;
}

void *
safe_realloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);

	if (ret == NULL)
		err(EX_OSERR, "realloc");
	return ret;
}

/*
 * Compare things like interface or table names.
 */
int
stringnum_cmp(const char *a, const char *b)
{
	int la, lb;

	la = strlen(a);
	lb = strlen(b);

	if (la > lb)
		return (1);
	else if (la < lb)
		return (-01);

	return (strcmp(a, b));
}


/*
 * conditionally runs the command.
 * Selected options or negative -> getsockopt
 */
int
do_cmd(int optname, void *optval, uintptr_t optlen)
{
	int i;

	if (co.test_only)
		return 0;

	if (ipfw_socket == -1)
		ipfw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (ipfw_socket < 0)
		err(EX_UNAVAILABLE, "socket");

	if (optname == IP_FW_GET || optname == IP_DUMMYNET_GET ||
	    optname == IP_FW_ADD || optname == IP_FW3 ||
	    optname == IP_FW_NAT_GET_CONFIG ||
	    optname < 0 ||
	    optname == IP_FW_NAT_GET_LOG) {
		if (optname < 0)
			optname = -optname;
		i = getsockopt(ipfw_socket, IPPROTO_IP, optname, optval,
			(socklen_t *)optlen);
	} else {
		i = setsockopt(ipfw_socket, IPPROTO_IP, optname, optval, optlen);
	}
	return i;
}

/*
 * do_set3 - pass ipfw control cmd to kernel
 * @optname: option name
 * @optval: pointer to option data
 * @optlen: option length
 *
 * Assumes op3 header is already embedded.
 * Calls setsockopt() with IP_FW3 as kernel-visible opcode.
 * Returns 0 on success or errno otherwise.
 */
int
do_set3(int optname, ip_fw3_opheader *op3, size_t optlen)
{

	if (co.test_only)
		return (0);

	if (ipfw_socket == -1)
		ipfw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (ipfw_socket < 0)
		err(EX_UNAVAILABLE, "socket");

	op3->opcode = optname;

	return (setsockopt(ipfw_socket, IPPROTO_IP, IP_FW3, op3, optlen));
}

/*
 * do_get3 - pass ipfw control cmd to kernel
 * @optname: option name
 * @optval: pointer to option data
 * @optlen: pointer to option length
 *
 * Assumes op3 header is already embedded.
 * Calls getsockopt() with IP_FW3 as kernel-visible opcode.
 * Returns 0 on success or errno otherwise.
 */
int
do_get3(int optname, ip_fw3_opheader *op3, size_t *optlen)
{
	int error;
	socklen_t len;

	if (co.test_only)
		return (0);

	if (ipfw_socket == -1)
		ipfw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (ipfw_socket < 0)
		err(EX_UNAVAILABLE, "socket");

	op3->opcode = optname;

	len = *optlen;
	error = getsockopt(ipfw_socket, IPPROTO_IP, IP_FW3, op3, &len);
	*optlen = len;

	return (error);
}

/**
 * match_token takes a table and a string, returns the value associated
 * with the string (-1 in case of failure).
 */
int
match_token(struct _s_x *table, const char *string)
{
	struct _s_x *pt;
	uint i = strlen(string);

	for (pt = table ; i && pt->s != NULL ; pt++)
		if (strlen(pt->s) == i && !bcmp(string, pt->s, i))
			return pt->x;
	return (-1);
}

/**
 * match_token_relaxed takes a table and a string, returns the value associated
 * with the string for the best match.
 *
 * Returns:
 * value from @table for matched records
 * -1 for non-matched records
 * -2 if more than one records match @string.
 */
int
match_token_relaxed(struct _s_x *table, const char *string)
{
	struct _s_x *pt, *m;
	int i, c;

	i = strlen(string);
	c = 0;

	for (pt = table ; i != 0 && pt->s != NULL ; pt++) {
		if (strncmp(pt->s, string, i) != 0)
			continue;
		m = pt;
		c++;
	}

	if (c == 1)
		return (m->x);

	return (c > 0 ? -2: -1);
}

int
get_token(struct _s_x *table, const char *string, const char *errbase)
{
	int tcmd;

	if ((tcmd = match_token_relaxed(table, string)) < 0)
		errx(EX_USAGE, "%s %s %s",
		    (tcmd == 0) ? "invalid" : "ambiguous", errbase, string);

	return (tcmd);
}

/**
 * match_value takes a table and a value, returns the string associated
 * with the value (NULL in case of failure).
 */
char const *
match_value(struct _s_x *p, int value)
{
	for (; p->s != NULL; p++)
		if (p->x == value)
			return p->s;
	return NULL;
}

size_t
concat_tokens(char *buf, size_t bufsize, struct _s_x *table, char *delimiter)
{
	struct _s_x *pt;
	int l;
	size_t sz;

	for (sz = 0, pt = table ; pt->s != NULL; pt++) {
		l = snprintf(buf + sz, bufsize - sz, "%s%s",
		    (sz == 0) ? "" : delimiter, pt->s);
		sz += l;
		bufsize += l;
		if (sz > bufsize)
			return (bufsize);
	}

	return (sz);
}

/*
 * helper function to process a set of flags and set bits in the
 * appropriate masks.
 */
int
fill_flags(struct _s_x *flags, char *p, char **e, uint32_t *set,
    uint32_t *clear)
{
	char *q;	/* points to the separator */
	int val;
	uint32_t *which;	/* mask we are working on */

	while (p && *p) {
		if (*p == '!') {
			p++;
			which = clear;
		} else
			which = set;
		q = strchr(p, ',');
		if (q)
			*q++ = '\0';
		val = match_token(flags, p);
		if (val <= 0) {
			if (e != NULL)
				*e = p;
			return (-1);
		}
		*which |= (uint32_t)val;
		p = q;
	}
	return (0);
}

void
print_flags_buffer(char *buf, size_t sz, struct _s_x *list, uint32_t set)
{
	char const *comma = "";
	int i, l;

	for (i = 0; list[i].x != 0; i++) {
		if ((set & list[i].x) == 0)
			continue;
		
		set &= ~list[i].x;
		l = snprintf(buf, sz, "%s%s", comma, list[i].s);
		if (l >= sz)
			return;
		comma = ",";
		buf += l;
		sz -=l;
	}
}

/*
 * _substrcmp takes two strings and returns 1 if they do not match,
 * and 0 if they match exactly or the first string is a sub-string
 * of the second.  A warning is printed to stderr in the case that the
 * first string is a sub-string of the second.
 *
 * This function will be removed in the future through the usual
 * deprecation process.
 */
int
_substrcmp(const char *str1, const char* str2)
{

	if (strncmp(str1, str2, strlen(str1)) != 0)
		return 1;

	if (strlen(str1) != strlen(str2))
		warnx("DEPRECATED: '%s' matched '%s' as a sub-string",
		    str1, str2);
	return 0;
}

/*
 * _substrcmp2 takes three strings and returns 1 if the first two do not match,
 * and 0 if they match exactly or the second string is a sub-string
 * of the first.  A warning is printed to stderr in the case that the
 * first string does not match the third.
 *
 * This function exists to warn about the bizarre construction
 * strncmp(str, "by", 2) which is used to allow people to use a shortcut
 * for "bytes".  The problem is that in addition to accepting "by",
 * "byt", "byte", and "bytes", it also excepts "by_rabid_dogs" and any
 * other string beginning with "by".
 *
 * This function will be removed in the future through the usual
 * deprecation process.
 */
int
_substrcmp2(const char *str1, const char* str2, const char* str3)
{

	if (strncmp(str1, str2, strlen(str2)) != 0)
		return 1;

	if (strcmp(str1, str3) != 0)
		warnx("DEPRECATED: '%s' matched '%s'",
		    str1, str3);
	return 0;
}

/*
 * prints one port, symbolic or numeric
 */
static void
print_port(struct buf_pr *bp, int proto, uint16_t port)
{

	if (proto == IPPROTO_ETHERTYPE) {
		char const *s;

		if (co.do_resolv && (s = match_value(ether_types, port)) )
			bprintf(bp, "%s", s);
		else
			bprintf(bp, "0x%04x", port);
	} else {
		struct servent *se = NULL;
		if (co.do_resolv) {
			struct protoent *pe = getprotobynumber(proto);

			se = getservbyport(htons(port), pe ? pe->p_name : NULL);
		}
		if (se)
			bprintf(bp, "%s", se->s_name);
		else
			bprintf(bp, "%d", port);
	}
}

static struct _s_x _port_name[] = {
	{"dst-port",	O_IP_DSTPORT},
	{"src-port",	O_IP_SRCPORT},
	{"ipid",	O_IPID},
	{"iplen",	O_IPLEN},
	{"ipttl",	O_IPTTL},
	{"mac-type",	O_MAC_TYPE},
	{"tcpdatalen",	O_TCPDATALEN},
	{"tcpwin",	O_TCPWIN},
	{"tagged",	O_TAGGED},
	{NULL,		0}
};

/*
 * Print the values in a list 16-bit items of the types above.
 * XXX todo: add support for mask.
 */
static void
print_newports(struct buf_pr *bp, ipfw_insn_u16 *cmd, int proto, int opcode)
{
	uint16_t *p = cmd->ports;
	int i;
	char const *sep;

	if (opcode != 0) {
		sep = match_value(_port_name, opcode);
		if (sep == NULL)
			sep = "???";
		bprintf(bp, " %s", sep);
	}
	sep = " ";
	for (i = F_LEN((ipfw_insn *)cmd) - 1; i > 0; i--, p += 2) {
		bprintf(bp, "%s", sep);
		print_port(bp, proto, p[0]);
		if (p[0] != p[1]) {
			bprintf(bp, "-");
			print_port(bp, proto, p[1]);
		}
		sep = ",";
	}
}

/*
 * Like strtol, but also translates service names into port numbers
 * for some protocols.
 * In particular:
 *	proto == -1 disables the protocol check;
 *	proto == IPPROTO_ETHERTYPE looks up an internal table
 *	proto == <some value in /etc/protocols> matches the values there.
 * Returns *end == s in case the parameter is not found.
 */
static int
strtoport(char *s, char **end, int base, int proto)
{
	char *p, *buf;
	char *s1;
	int i;

	*end = s;		/* default - not found */
	if (*s == '\0')
		return 0;	/* not found */

	if (isdigit(*s))
		return strtol(s, end, base);

	/*
	 * find separator. '\\' escapes the next char.
	 */
	for (s1 = s; *s1 && (isalnum(*s1) || *s1 == '\\' ||
	    *s1 == '_' || *s1 == '.') ; s1++)
		if (*s1 == '\\' && s1[1] != '\0')
			s1++;

	buf = safe_calloc(s1 - s + 1, 1);

	/*
	 * copy into a buffer skipping backslashes
	 */
	for (p = s, i = 0; p != s1 ; p++)
		if (*p != '\\')
			buf[i++] = *p;
	buf[i++] = '\0';

	if (proto == IPPROTO_ETHERTYPE) {
		i = match_token(ether_types, buf);
		free(buf);
		if (i != -1) {	/* found */
			*end = s1;
			return i;
		}
	} else {
		struct protoent *pe = NULL;
		struct servent *se;

		if (proto != 0)
			pe = getprotobynumber(proto);
		setservent(1);
		se = getservbyname(buf, pe ? pe->p_name : NULL);
		free(buf);
		if (se != NULL) {
			*end = s1;
			return ntohs(se->s_port);
		}
	}
	return 0;	/* not found */
}

/*
 * Fill the body of the command with the list of port ranges.
 */
static int
fill_newports(ipfw_insn_u16 *cmd, char *av, int proto, int cblen)
{
	uint16_t a, b, *p = cmd->ports;
	int i = 0;
	char *s = av;

	while (*s) {
		a = strtoport(av, &s, 0, proto);
		if (s == av) 			/* empty or invalid argument */
			return (0);

		CHECK_LENGTH(cblen, i + 2);

		switch (*s) {
		case '-':			/* a range */
			av = s + 1;
			b = strtoport(av, &s, 0, proto);
			/* Reject expressions like '1-abc' or '1-2-3'. */
			if (s == av || (*s != ',' && *s != '\0'))
				return (0);
			p[0] = a;
			p[1] = b;
			break;
		case ',':			/* comma separated list */
		case '\0':
			p[0] = p[1] = a;
			break;
		default:
			warnx("port list: invalid separator <%c> in <%s>",
				*s, av);
			return (0);
		}

		i++;
		p += 2;
		av = s + 1;
	}
	if (i > 0) {
		if (i + 1 > F_LEN_MASK)
			errx(EX_DATAERR, "too many ports/ranges\n");
		cmd->o.len |= i + 1;	/* leave F_NOT and F_OR untouched */
	}
	return (i);
}

/*
 * Fill the body of the command with the list of DiffServ codepoints.
 */
static void
fill_dscp(ipfw_insn *cmd, char *av, int cblen)
{
	uint32_t *low, *high;
	char *s = av, *a;
	int code;

	cmd->opcode = O_DSCP;
	cmd->len |= F_INSN_SIZE(ipfw_insn_u32) + 1;

	CHECK_CMDLEN;

	low = (uint32_t *)(cmd + 1);
	high = low + 1;

	*low = 0;
	*high = 0;

	while (s != NULL) {
		a = strchr(s, ',');

		if (a != NULL)
			*a++ = '\0';

		if (isalpha(*s)) {
			if ((code = match_token(f_ipdscp, s)) == -1)
				errx(EX_DATAERR, "Unknown DSCP code");
		} else {
			code = strtoul(s, NULL, 10);
			if (code < 0 || code > 63)
				errx(EX_DATAERR, "Invalid DSCP value");
		}

		if (code >= 32)
			*high |= 1 << (code - 32);
		else
			*low |= 1 << code;

		s = a;
	}
}

static struct _s_x icmpcodes[] = {
      { "net",			ICMP_UNREACH_NET },
      { "host",			ICMP_UNREACH_HOST },
      { "protocol",		ICMP_UNREACH_PROTOCOL },
      { "port",			ICMP_UNREACH_PORT },
      { "needfrag",		ICMP_UNREACH_NEEDFRAG },
      { "srcfail",		ICMP_UNREACH_SRCFAIL },
      { "net-unknown",		ICMP_UNREACH_NET_UNKNOWN },
      { "host-unknown",		ICMP_UNREACH_HOST_UNKNOWN },
      { "isolated",		ICMP_UNREACH_ISOLATED },
      { "net-prohib",		ICMP_UNREACH_NET_PROHIB },
      { "host-prohib",		ICMP_UNREACH_HOST_PROHIB },
      { "tosnet",		ICMP_UNREACH_TOSNET },
      { "toshost",		ICMP_UNREACH_TOSHOST },
      { "filter-prohib",	ICMP_UNREACH_FILTER_PROHIB },
      { "host-precedence",	ICMP_UNREACH_HOST_PRECEDENCE },
      { "precedence-cutoff",	ICMP_UNREACH_PRECEDENCE_CUTOFF },
      { NULL, 0 }
};

static void
fill_reject_code(u_short *codep, char *str)
{
	int val;
	char *s;

	val = strtoul(str, &s, 0);
	if (s == str || *s != '\0' || val >= 0x100)
		val = match_token(icmpcodes, str);
	if (val < 0)
		errx(EX_DATAERR, "unknown ICMP unreachable code ``%s''", str);
	*codep = val;
	return;
}

static void
print_reject_code(struct buf_pr *bp, uint16_t code)
{
	char const *s;

	if ((s = match_value(icmpcodes, code)) != NULL)
		bprintf(bp, "unreach %s", s);
	else
		bprintf(bp, "unreach %u", code);
}

/*
 * Returns the number of bits set (from left) in a contiguous bitmask,
 * or -1 if the mask is not contiguous.
 * XXX this needs a proper fix.
 * This effectively works on masks in big-endian (network) format.
 * when compiled on little endian architectures.
 *
 * First bit is bit 7 of the first byte -- note, for MAC addresses,
 * the first bit on the wire is bit 0 of the first byte.
 * len is the max length in bits.
 */
int
contigmask(uint8_t *p, int len)
{
	int i, n;

	for (i=0; i<len ; i++)
		if ( (p[i/8] & (1 << (7 - (i%8)))) == 0) /* first bit unset */
			break;
	for (n=i+1; n < len; n++)
		if ( (p[n/8] & (1 << (7 - (n%8)))) != 0)
			return -1; /* mask not contiguous */
	return i;
}

/*
 * print flags set/clear in the two bitmasks passed as parameters.
 * There is a specialized check for f_tcpflags.
 */
static void
print_flags(struct buf_pr *bp, char const *name, ipfw_insn *cmd,
    struct _s_x *list)
{
	char const *comma = "";
	int i;
	uint8_t set = cmd->arg1 & 0xff;
	uint8_t clear = (cmd->arg1 >> 8) & 0xff;

	if (list == f_tcpflags && set == TH_SYN && clear == TH_ACK) {
		bprintf(bp, " setup");
		return;
	}

	bprintf(bp, " %s ", name);
	for (i=0; list[i].x != 0; i++) {
		if (set & list[i].x) {
			set &= ~list[i].x;
			bprintf(bp, "%s%s", comma, list[i].s);
			comma = ",";
		}
		if (clear & list[i].x) {
			clear &= ~list[i].x;
			bprintf(bp, "%s!%s", comma, list[i].s);
			comma = ",";
		}
	}
}


/*
 * Print the ip address contained in a command.
 */
static void
print_ip(struct buf_pr *bp, const struct format_opts *fo, ipfw_insn_ip *cmd)
{
	struct hostent *he = NULL;
	struct in_addr *ia;
	uint32_t len = F_LEN((ipfw_insn *)cmd);
	uint32_t *a = ((ipfw_insn_u32 *)cmd)->d;
	char *t;

	bprintf(bp, " ");
	if (cmd->o.opcode == O_IP_DST_LOOKUP && len > F_INSN_SIZE(ipfw_insn_u32)) {
		uint32_t d = a[1];
		const char *arg = "<invalid>";

		if (d < sizeof(lookup_key)/sizeof(lookup_key[0]))
			arg = match_value(rule_options, lookup_key[d]);
		t = table_search_ctlv(fo->tstate, ((ipfw_insn *)cmd)->arg1);
		bprintf(bp, "lookup %s %s", arg, t);
		return;
	}
	if (cmd->o.opcode == O_IP_SRC_ME || cmd->o.opcode == O_IP_DST_ME) {
		bprintf(bp, "me");
		return;
	}
	if (cmd->o.opcode == O_IP_SRC_LOOKUP ||
	    cmd->o.opcode == O_IP_DST_LOOKUP) {
		t = table_search_ctlv(fo->tstate, ((ipfw_insn *)cmd)->arg1);
		bprintf(bp, "table(%s", t);
		if (len == F_INSN_SIZE(ipfw_insn_u32))
			bprintf(bp, ",%u", *a);
		bprintf(bp, ")");
		return;
	}
	if (cmd->o.opcode == O_IP_SRC_SET || cmd->o.opcode == O_IP_DST_SET) {
		uint32_t x, *map = (uint32_t *)&(cmd->mask);
		int i, j;
		char comma = '{';

		x = cmd->o.arg1 - 1;
		x = htonl( ~x );
		cmd->addr.s_addr = htonl(cmd->addr.s_addr);
		bprintf(bp, "%s/%d", inet_ntoa(cmd->addr),
			contigmask((uint8_t *)&x, 32));
		x = cmd->addr.s_addr = htonl(cmd->addr.s_addr);
		x &= 0xff; /* base */
		/*
		 * Print bits and ranges.
		 * Locate first bit set (i), then locate first bit unset (j).
		 * If we have 3+ consecutive bits set, then print them as a
		 * range, otherwise only print the initial bit and rescan.
		 */
		for (i=0; i < cmd->o.arg1; i++)
			if (map[i/32] & (1<<(i & 31))) {
				for (j=i+1; j < cmd->o.arg1; j++)
					if (!(map[ j/32] & (1<<(j & 31))))
						break;
				bprintf(bp, "%c%d", comma, i+x);
				if (j>i+2) { /* range has at least 3 elements */
					bprintf(bp, "-%d", j-1+x);
					i = j-1;
				}
				comma = ',';
			}
		bprintf(bp, "}");
		return;
	}
	/*
	 * len == 2 indicates a single IP, whereas lists of 1 or more
	 * addr/mask pairs have len = (2n+1). We convert len to n so we
	 * use that to count the number of entries.
	 */
    for (len = len / 2; len > 0; len--, a += 2) {
	int mb =	/* mask length */
	    (cmd->o.opcode == O_IP_SRC || cmd->o.opcode == O_IP_DST) ?
		32 : contigmask((uint8_t *)&(a[1]), 32);
	if (mb == 32 && co.do_resolv)
		he = gethostbyaddr((char *)&(a[0]), sizeof(in_addr_t),
		    AF_INET);
	if (he != NULL)		/* resolved to name */
		bprintf(bp, "%s", he->h_name);
	else if (mb == 0)	/* any */
		bprintf(bp, "any");
	else {		/* numeric IP followed by some kind of mask */
		ia = (struct in_addr *)&a[0];
		bprintf(bp, "%s", inet_ntoa(*ia));
		if (mb < 0) {
			ia = (struct in_addr *)&a[1];
			bprintf(bp, ":%s", inet_ntoa(*ia));
		} else if (mb < 32)
			bprintf(bp, "/%d", mb);
	}
	if (len > 1)
		bprintf(bp, ",");
    }
}

/*
 * prints a MAC address/mask pair
 */
static void
format_mac(struct buf_pr *bp, uint8_t *addr, uint8_t *mask)
{
	int l = contigmask(mask, 48);

	if (l == 0)
		bprintf(bp, " any");
	else {
		bprintf(bp, " %02x:%02x:%02x:%02x:%02x:%02x",
		    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
		if (l == -1)
			bprintf(bp, "&%02x:%02x:%02x:%02x:%02x:%02x",
			    mask[0], mask[1], mask[2],
			    mask[3], mask[4], mask[5]);
		else if (l < 48)
			bprintf(bp, "/%d", l);
	}
}

static void
print_mac(struct buf_pr *bp, ipfw_insn_mac *mac)
{

	bprintf(bp, " MAC");
	format_mac(bp, mac->addr, mac->mask);
	format_mac(bp, mac->addr + 6, mac->mask + 6);
}

static void
fill_icmptypes(ipfw_insn_u32 *cmd, char *av)
{
	uint8_t type;

	cmd->d[0] = 0;
	while (*av) {
		if (*av == ',')
			av++;

		type = strtoul(av, &av, 0);

		if (*av != ',' && *av != '\0')
			errx(EX_DATAERR, "invalid ICMP type");

		if (type > 31)
			errx(EX_DATAERR, "ICMP type out of range");

		cmd->d[0] |= 1 << type;
	}
	cmd->o.opcode = O_ICMPTYPE;
	cmd->o.len |= F_INSN_SIZE(ipfw_insn_u32);
}

static void
print_icmptypes(struct buf_pr *bp, ipfw_insn_u32 *cmd)
{
	int i;
	char sep= ' ';

	bprintf(bp, " icmptypes");
	for (i = 0; i < 32; i++) {
		if ( (cmd->d[0] & (1 << (i))) == 0)
			continue;
		bprintf(bp, "%c%d", sep, i);
		sep = ',';
	}
}

static void
print_dscp(struct buf_pr *bp, ipfw_insn_u32 *cmd)
{
	int i = 0;
	uint32_t *v;
	char sep= ' ';
	const char *code;

	bprintf(bp, " dscp");
	v = cmd->d;
	while (i < 64) {
		if (*v & (1 << i)) {
			if ((code = match_value(f_ipdscp, i)) != NULL)
				bprintf(bp, "%c%s", sep, code);
			else
				bprintf(bp, "%c%d", sep, i);
			sep = ',';
		}

		if ((++i % 32) == 0)
			v++;
	}
}

#define	insntod(cmd, type)	((ipfw_insn_ ## type *)(cmd))
struct show_state {
	struct ip_fw_rule	*rule;
	const ipfw_insn		*eaction;
	uint8_t			*printed;
	int			flags;
#define	HAVE_PROTO		0x0001
#define	HAVE_SRCIP		0x0002
#define	HAVE_DSTIP		0x0004
#define	HAVE_PROBE_STATE	0x0008
	int			proto;
	int			or_block;
};

static int
init_show_state(struct show_state *state, struct ip_fw_rule *rule)
{

	state->printed = calloc(rule->cmd_len, sizeof(uint8_t));
	if (state->printed == NULL)
		return (ENOMEM);
	state->rule = rule;
	state->eaction = NULL;
	state->flags = 0;
	state->proto = 0;
	state->or_block = 0;
	return (0);
}

static void
free_show_state(struct show_state *state)
{

	free(state->printed);
}

static uint8_t
is_printed_opcode(struct show_state *state, const ipfw_insn *cmd)
{

	return (state->printed[cmd - state->rule->cmd]);
}

static void
mark_printed(struct show_state *state, const ipfw_insn *cmd)
{

	state->printed[cmd - state->rule->cmd] = 1;
}

static void
print_limit_mask(struct buf_pr *bp, const ipfw_insn_limit *limit)
{
	struct _s_x *p = limit_masks;
	char const *comma = " ";
	uint8_t x;

	for (x = limit->limit_mask; p->x != 0; p++) {
		if ((x & p->x) == p->x) {
			x &= ~p->x;
			bprintf(bp, "%s%s", comma, p->s);
			comma = ",";
		}
	}
	bprint_uint_arg(bp, " ", limit->conn_limit);
}

static int
print_instruction(struct buf_pr *bp, const struct format_opts *fo,
    struct show_state *state, ipfw_insn *cmd)
{
	struct protoent *pe;
	struct passwd *pwd;
	struct group *grp;
	const char *s;
	double d;

	if (is_printed_opcode(state, cmd))
		return (0);
	if ((cmd->len & F_OR) != 0 && state->or_block == 0)
		bprintf(bp, " {");
	if (cmd->opcode != O_IN && (cmd->len & F_NOT) != 0)
		bprintf(bp, " not");

	switch (cmd->opcode) {
	case O_PROB:
		d = 1.0 * insntod(cmd, u32)->d[0] / 0x7fffffff;
		bprintf(bp, "prob %f ", d);
		break;
	case O_PROBE_STATE: /* no need to print anything here */
		state->flags |= HAVE_PROBE_STATE;
		break;
	case O_IP_SRC:
	case O_IP_SRC_LOOKUP:
	case O_IP_SRC_MASK:
	case O_IP_SRC_ME:
	case O_IP_SRC_SET:
		if (state->flags & HAVE_SRCIP)
			bprintf(bp, " src-ip");
		print_ip(bp, fo, insntod(cmd, ip));
		break;
	case O_IP_DST:
	case O_IP_DST_LOOKUP:
	case O_IP_DST_MASK:
	case O_IP_DST_ME:
	case O_IP_DST_SET:
		if (state->flags & HAVE_DSTIP)
			bprintf(bp, " dst-ip");
		print_ip(bp, fo, insntod(cmd, ip));
		break;
	case O_IP6_SRC:
	case O_IP6_SRC_MASK:
	case O_IP6_SRC_ME:
		if (state->flags & HAVE_SRCIP)
			bprintf(bp, " src-ip6");
		print_ip6(bp, insntod(cmd, ip6));
		break;
	case O_IP6_DST:
	case O_IP6_DST_MASK:
	case O_IP6_DST_ME:
		if (state->flags & HAVE_DSTIP)
			bprintf(bp, " dst-ip6");
		print_ip6(bp, insntod(cmd, ip6));
		break;
	case O_FLOW6ID:
		print_flow6id(bp, insntod(cmd, u32));
		break;
	case O_IP_DSTPORT:
	case O_IP_SRCPORT:
		print_newports(bp, insntod(cmd, u16), state->proto,
		    (state->flags & (HAVE_SRCIP | HAVE_DSTIP)) ==
		    (HAVE_SRCIP | HAVE_DSTIP) ?  cmd->opcode: 0);
		break;
	case O_PROTO:
		pe = getprotobynumber(cmd->arg1);
		if (state->flags & HAVE_PROTO)
			bprintf(bp, " proto");
		if (pe != NULL)
			bprintf(bp, " %s", pe->p_name);
		else
			bprintf(bp, " %u", cmd->arg1);
		state->proto = cmd->arg1;
		break;
	case O_MACADDR2:
		print_mac(bp, insntod(cmd, mac));
		break;
	case O_MAC_TYPE:
		print_newports(bp, insntod(cmd, u16),
		    IPPROTO_ETHERTYPE, cmd->opcode);
		break;
	case O_FRAG:
		bprintf(bp, " frag");
		break;
	case O_FIB:
		bprintf(bp, " fib %u", cmd->arg1);
		break;
	case O_SOCKARG:
		bprintf(bp, " sockarg");
		break;
	case O_IN:
		bprintf(bp, cmd->len & F_NOT ? " out" : " in");
		break;
	case O_DIVERTED:
		switch (cmd->arg1) {
		case 3:
			bprintf(bp, " diverted");
			break;
		case 2:
			bprintf(bp, " diverted-output");
			break;
		case 1:
			bprintf(bp, " diverted-loopback");
			break;
		default:
			bprintf(bp, " diverted-?<%u>", cmd->arg1);
			break;
		}
		break;
	case O_LAYER2:
		bprintf(bp, " layer2");
		break;
	case O_XMIT:
	case O_RECV:
	case O_VIA:
		if (cmd->opcode == O_XMIT)
			s = "xmit";
		else if (cmd->opcode == O_RECV)
			s = "recv";
		else /* if (cmd->opcode == O_VIA) */
			s = "via";
		switch (insntod(cmd, if)->name[0]) {
		case '\0':
			bprintf(bp, " %s %s", s,
			    inet_ntoa(insntod(cmd, if)->p.ip));
			break;
		case '\1':
			bprintf(bp, " %s table(%s)", s,
			    table_search_ctlv(fo->tstate,
			    insntod(cmd, if)->p.kidx));
			break;
		default:
			bprintf(bp, " %s %s", s,
			    insntod(cmd, if)->name);
		}
		break;
	case O_IP_FLOW_LOOKUP:
		s = table_search_ctlv(fo->tstate, cmd->arg1);
		bprintf(bp, " flow table(%s", s);
		if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_u32))
			bprintf(bp, ",%u", insntod(cmd, u32)->d[0]);
		bprintf(bp, ")");
		break;
	case O_IPID:
	case O_IPTTL:
	case O_IPLEN:
	case O_TCPDATALEN:
	case O_TCPWIN:
		if (F_LEN(cmd) == 1) {
			switch (cmd->opcode) {
			case O_IPID:
				s = "ipid";
				break;
			case O_IPTTL:
				s = "ipttl";
				break;
			case O_IPLEN:
				s = "iplen";
				break;
			case O_TCPDATALEN:
				s = "tcpdatalen";
				break;
			case O_TCPWIN:
				s = "tcpwin";
				break;
			}
			bprintf(bp, " %s %u", s, cmd->arg1);
		} else
			print_newports(bp, insntod(cmd, u16), 0,
			    cmd->opcode);
		break;
	case O_IPVER:
		bprintf(bp, " ipver %u", cmd->arg1);
		break;
	case O_IPPRECEDENCE:
		bprintf(bp, " ipprecedence %u", cmd->arg1 >> 5);
		break;
	case O_DSCP:
		print_dscp(bp, insntod(cmd, u32));
		break;
	case O_IPOPT:
		print_flags(bp, "ipoptions", cmd, f_ipopts);
		break;
	case O_IPTOS:
		print_flags(bp, "iptos", cmd, f_iptos);
		break;
	case O_ICMPTYPE:
		print_icmptypes(bp, insntod(cmd, u32));
		break;
	case O_ESTAB:
		bprintf(bp, " established");
		break;
	case O_TCPFLAGS:
		print_flags(bp, "tcpflags", cmd, f_tcpflags);
		break;
	case O_TCPOPTS:
		print_flags(bp, "tcpoptions", cmd, f_tcpopts);
		break;
	case O_TCPACK:
		bprintf(bp, " tcpack %d",
		    ntohl(insntod(cmd, u32)->d[0]));
		break;
	case O_TCPSEQ:
		bprintf(bp, " tcpseq %d",
		    ntohl(insntod(cmd, u32)->d[0]));
		break;
	case O_UID:
		pwd = getpwuid(insntod(cmd, u32)->d[0]);
		if (pwd != NULL)
			bprintf(bp, " uid %s", pwd->pw_name);
		else
			bprintf(bp, " uid %u",
			    insntod(cmd, u32)->d[0]);
		break;
	case O_GID:
		grp = getgrgid(insntod(cmd, u32)->d[0]);
		if (grp != NULL)
			bprintf(bp, " gid %s", grp->gr_name);
		else
			bprintf(bp, " gid %u",
			    insntod(cmd, u32)->d[0]);
		break;
	case O_JAIL:
		bprintf(bp, " jail %d", insntod(cmd, u32)->d[0]);
		break;
	case O_VERREVPATH:
		bprintf(bp, " verrevpath");
		break;
	case O_VERSRCREACH:
		bprintf(bp, " versrcreach");
		break;
	case O_ANTISPOOF:
		bprintf(bp, " antispoof");
		break;
	case O_IPSEC:
		bprintf(bp, " ipsec");
		break;
	case O_NOP:
		bprintf(bp, " // %s", (char *)(cmd + 1));
		break;
	case O_KEEP_STATE:
		if (state->flags & HAVE_PROBE_STATE)
			bprintf(bp, " keep-state");
		else
			bprintf(bp, " record-state");
		bprintf(bp, " :%s",
		    object_search_ctlv(fo->tstate, cmd->arg1,
		    IPFW_TLV_STATE_NAME));
		break;
	case O_LIMIT:
		if (state->flags & HAVE_PROBE_STATE)
			bprintf(bp, " limit");
		else
			bprintf(bp, " set-limit");
		print_limit_mask(bp, insntod(cmd, limit));
		bprintf(bp, " :%s",
		    object_search_ctlv(fo->tstate, cmd->arg1,
		    IPFW_TLV_STATE_NAME));
		break;
	case O_IP6:
		bprintf(bp, " ip6");
		break;
	case O_IP4:
		bprintf(bp, " ip4");
		break;
	case O_ICMP6TYPE:
		print_icmp6types(bp, insntod(cmd, u32));
		break;
	case O_EXT_HDR:
		print_ext6hdr(bp, cmd);
		break;
	case O_TAGGED:
		if (F_LEN(cmd) == 1)
			bprint_uint_arg(bp, " tagged ", cmd->arg1);
		else
			print_newports(bp, insntod(cmd, u16),
				    0, O_TAGGED);
		break;
	case O_SKIP_ACTION:
		bprintf(bp, " defer-immediate-action");
		break;
	default:
		bprintf(bp, " [opcode %d len %d]", cmd->opcode,
		    cmd->len);
	}
	if (cmd->len & F_OR) {
		bprintf(bp, " or");
		state->or_block = 1;
	} else if (state->or_block != 0) {
		bprintf(bp, " }");
		state->or_block = 0;
	}
	mark_printed(state, cmd);

	return (1);
}

static ipfw_insn *
print_opcode(struct buf_pr *bp, struct format_opts *fo,
    struct show_state *state, int opcode)
{
	ipfw_insn *cmd;
	int l;

	for (l = state->rule->act_ofs, cmd = state->rule->cmd;
	    l > 0; l -= F_LEN(cmd), cmd += F_LEN(cmd)) {
		/* We use zero opcode to print the rest of options */
		if (opcode >= 0 && cmd->opcode != opcode)
			continue;
		/*
		 * Skip O_NOP, when we printing the rest
		 * of options, it will be handled separately.
		 */
		if (cmd->opcode == O_NOP && opcode != O_NOP)
			continue;
		if (!print_instruction(bp, fo, state, cmd))
			continue;
		return (cmd);
	}
	return (NULL);
}

static void
print_fwd(struct buf_pr *bp, const ipfw_insn *cmd)
{
	char buf[INET6_ADDRSTRLEN + IF_NAMESIZE + 2];
	ipfw_insn_sa6 *sa6;
	ipfw_insn_sa *sa;
	uint16_t port;

	if (cmd->opcode == O_FORWARD_IP) {
		sa = insntod(cmd, sa);
		port = sa->sa.sin_port;
		if (sa->sa.sin_addr.s_addr == INADDR_ANY)
			bprintf(bp, "fwd tablearg");
		else
			bprintf(bp, "fwd %s", inet_ntoa(sa->sa.sin_addr));
	} else {
		sa6 = insntod(cmd, sa6);
		port = sa6->sa.sin6_port;
		bprintf(bp, "fwd ");
		if (getnameinfo((const struct sockaddr *)&sa6->sa,
		    sizeof(struct sockaddr_in6), buf, sizeof(buf), NULL, 0,
		    NI_NUMERICHOST) == 0)
			bprintf(bp, "%s", buf);
	}
	if (port != 0)
		bprintf(bp, ",%u", port);
}

static int
print_action_instruction(struct buf_pr *bp, const struct format_opts *fo,
    struct show_state *state, const ipfw_insn *cmd)
{
	const char *s;

	if (is_printed_opcode(state, cmd))
		return (0);
	switch (cmd->opcode) {
	case O_CHECK_STATE:
		bprintf(bp, "check-state");
		if (cmd->arg1 != 0)
			s = object_search_ctlv(fo->tstate, cmd->arg1,
			    IPFW_TLV_STATE_NAME);
		else
			s = NULL;
		bprintf(bp, " :%s", s ? s: "any");
		break;
	case O_ACCEPT:
		bprintf(bp, "allow");
		break;
	case O_COUNT:
		bprintf(bp, "count");
		break;
	case O_DENY:
		bprintf(bp, "deny");
		break;
	case O_REJECT:
		if (cmd->arg1 == ICMP_REJECT_RST)
			bprintf(bp, "reset");
		else if (cmd->arg1 == ICMP_REJECT_ABORT)
			bprintf(bp, "abort");
		else if (cmd->arg1 == ICMP_UNREACH_HOST)
			bprintf(bp, "reject");
		else
			print_reject_code(bp, cmd->arg1);
		break;
	case O_UNREACH6:
		if (cmd->arg1 == ICMP6_UNREACH_RST)
			bprintf(bp, "reset6");
		else if (cmd->arg1 == ICMP6_UNREACH_ABORT)
			bprintf(bp, "abort6");
		else
			print_unreach6_code(bp, cmd->arg1);
		break;
	case O_SKIPTO:
		bprint_uint_arg(bp, "skipto ", cmd->arg1);
		break;
	case O_PIPE:
		bprint_uint_arg(bp, "pipe ", cmd->arg1);
		break;
	case O_QUEUE:
		bprint_uint_arg(bp, "queue ", cmd->arg1);
		break;
	case O_DIVERT:
		bprint_uint_arg(bp, "divert ", cmd->arg1);
		break;
	case O_TEE:
		bprint_uint_arg(bp, "tee ", cmd->arg1);
		break;
	case O_NETGRAPH:
		bprint_uint_arg(bp, "netgraph ", cmd->arg1);
		break;
	case O_NGTEE:
		bprint_uint_arg(bp, "ngtee ", cmd->arg1);
		break;
	case O_FORWARD_IP:
	case O_FORWARD_IP6:
		print_fwd(bp, cmd);
		break;
	case O_LOG:
		if (insntod(cmd, log)->max_log > 0)
			bprintf(bp, " log logamount %d",
			    insntod(cmd, log)->max_log);
		else
			bprintf(bp, " log");
		break;
	case O_ALTQ:
#ifndef NO_ALTQ
		print_altq_cmd(bp, insntod(cmd, altq));
#endif
		break;
	case O_TAG:
		bprint_uint_arg(bp, cmd->len & F_NOT ? " untag ":
		    " tag ", cmd->arg1);
		break;
	case O_NAT:
		if (cmd->arg1 != IP_FW_NAT44_GLOBAL)
			bprint_uint_arg(bp, "nat ", cmd->arg1);
		else
			bprintf(bp, "nat global");
		break;
	case O_SETFIB:
		if (cmd->arg1 == IP_FW_TARG)
			bprint_uint_arg(bp, "setfib ", cmd->arg1);
		else
			bprintf(bp, "setfib %u", cmd->arg1 & 0x7FFF);
		break;
	case O_EXTERNAL_ACTION:
		/*
		 * The external action can consists of two following
		 * each other opcodes - O_EXTERNAL_ACTION and
		 * O_EXTERNAL_INSTANCE. The first contains the ID of
		 * name of external action. The second contains the ID
		 * of name of external action instance.
		 * NOTE: in case when external action has no named
		 * instances support, the second opcode isn't needed.
		 */
		state->eaction = cmd;
		s = object_search_ctlv(fo->tstate, cmd->arg1,
		    IPFW_TLV_EACTION);
		if (match_token(rule_eactions, s) != -1)
			bprintf(bp, "%s", s);
		else
			bprintf(bp, "eaction %s", s);
		break;
	case O_EXTERNAL_INSTANCE:
		if (state->eaction == NULL)
			break;
		/*
		 * XXX: we need to teach ipfw(9) to rewrite opcodes
		 * in the user buffer on rule addition. When we add
		 * the rule, we specify zero TLV type for
		 * O_EXTERNAL_INSTANCE object. To show correct
		 * rule after `ipfw add` we need to search instance
		 * name with zero type. But when we do `ipfw show`
		 * we calculate TLV type using IPFW_TLV_EACTION_NAME()
		 * macro.
		 */
		s = object_search_ctlv(fo->tstate, cmd->arg1, 0);
		if (s == NULL)
			s = object_search_ctlv(fo->tstate,
			    cmd->arg1, IPFW_TLV_EACTION_NAME(
			    state->eaction->arg1));
		bprintf(bp, " %s", s);
		break;
	case O_EXTERNAL_DATA:
		if (state->eaction == NULL)
			break;
		/*
		 * Currently we support data formatting only for
		 * external data with datalen u16. For unknown data
		 * print its size in bytes.
		 */
		if (cmd->len == F_INSN_SIZE(ipfw_insn))
			bprintf(bp, " %u", cmd->arg1);
		else
			bprintf(bp, " %ubytes",
			    cmd->len * sizeof(uint32_t));
		break;
	case O_SETDSCP:
		if (cmd->arg1 == IP_FW_TARG) {
			bprintf(bp, "setdscp tablearg");
			break;
		}
		s = match_value(f_ipdscp, cmd->arg1 & 0x3F);
		if (s != NULL)
			bprintf(bp, "setdscp %s", s);
		else
			bprintf(bp, "setdscp %u", cmd->arg1 & 0x3F);
		break;
	case O_REASS:
		bprintf(bp, "reass");
		break;
	case O_CALLRETURN:
		if (cmd->len & F_NOT)
			bprintf(bp, "return");
		else
			bprint_uint_arg(bp, "call ", cmd->arg1);
		break;
	default:
		bprintf(bp, "** unrecognized action %d len %d ",
			cmd->opcode, cmd->len);
	}
	mark_printed(state, cmd);

	return (1);
}


static ipfw_insn *
print_action(struct buf_pr *bp, struct format_opts *fo,
    struct show_state *state, uint8_t opcode)
{
	ipfw_insn *cmd;
	int l;

	for (l = state->rule->cmd_len - state->rule->act_ofs,
	    cmd = ACTION_PTR(state->rule); l > 0;
	    l -= F_LEN(cmd), cmd += F_LEN(cmd)) {
		if (cmd->opcode != opcode)
			continue;
		if (!print_action_instruction(bp, fo, state, cmd))
			continue;
		return (cmd);
	}
	return (NULL);
}

static void
print_proto(struct buf_pr *bp, struct format_opts *fo,
    struct show_state *state)
{
	ipfw_insn *cmd;
	int l, proto, ip4, ip6;

	/* Count all O_PROTO, O_IP4, O_IP6 instructions. */
	proto = ip4 = ip6 = 0;
	for (l = state->rule->act_ofs, cmd = state->rule->cmd;
	    l > 0; l -= F_LEN(cmd), cmd += F_LEN(cmd)) {
		switch (cmd->opcode) {
		case O_PROTO:
			proto++;
			break;
		case O_IP4:
			ip4 = 1;
			if (cmd->len & F_OR)
				ip4++;
			break;
		case O_IP6:
			ip6 = 1;
			if (cmd->len & F_OR)
				ip6++;
			break;
		default:
			continue;
		}
	}
	if (proto == 0 && ip4 == 0 && ip6 == 0) {
		state->proto = IPPROTO_IP;
		state->flags |= HAVE_PROTO;
		bprintf(bp, " ip");
		return;
	}
	/* To handle the case { ip4 or ip6 }, print opcode with F_OR first */
	cmd = NULL;
	if (ip4 || ip6)
		cmd = print_opcode(bp, fo, state, ip4 > ip6 ? O_IP4: O_IP6);
	if (cmd != NULL && (cmd->len & F_OR))
		cmd = print_opcode(bp, fo, state, ip4 > ip6 ? O_IP6: O_IP4);
	if (cmd == NULL || (cmd->len & F_OR))
		for (l = proto; l > 0; l--) {
			cmd = print_opcode(bp, fo, state, O_PROTO);
			if (cmd == NULL || (cmd->len & F_OR) == 0)
				break;
		}
	/* Initialize proto, it is used by print_newports() */
	state->flags |= HAVE_PROTO;
	if (state->proto == 0 && ip6 != 0)
		state->proto = IPPROTO_IPV6;
}

static int
match_opcode(int opcode, const int opcodes[], size_t nops)
{
	int i;

	for (i = 0; i < nops; i++)
		if (opcode == opcodes[i])
			return (1);
	return (0);
}

static void
print_address(struct buf_pr *bp, struct format_opts *fo,
    struct show_state *state, const int opcodes[], size_t nops, int portop,
    int flag)
{
	ipfw_insn *cmd;
	int count, l, portcnt, pf;

	count = portcnt = 0;
	for (l = state->rule->act_ofs, cmd = state->rule->cmd;
	    l > 0; l -= F_LEN(cmd), cmd += F_LEN(cmd)) {
		if (match_opcode(cmd->opcode, opcodes, nops))
			count++;
		else if (cmd->opcode == portop)
			portcnt++;
	}
	if (count == 0)
		bprintf(bp, " any");
	for (l = state->rule->act_ofs, cmd = state->rule->cmd;
	    l > 0 && count > 0; l -= F_LEN(cmd), cmd += F_LEN(cmd)) {
		if (!match_opcode(cmd->opcode, opcodes, nops))
			continue;
		print_instruction(bp, fo, state, cmd);
		if ((cmd->len & F_OR) == 0)
			break;
		count--;
	}
	/*
	 * If several O_IP_?PORT opcodes specified, leave them to the
	 * options section.
	 */
	if (portcnt == 1) {
		for (l = state->rule->act_ofs, cmd = state->rule->cmd, pf = 0;
		    l > 0; l -= F_LEN(cmd), cmd += F_LEN(cmd)) {
			if (cmd->opcode != portop) {
				pf = (cmd->len & F_OR);
				continue;
			}
			/* Print opcode iff it is not in OR block. */
			if (pf == 0 && (cmd->len & F_OR) == 0)
				print_instruction(bp, fo, state, cmd);
			break;
		}
	}
	state->flags |= flag;
}

static const int action_opcodes[] = {
	O_CHECK_STATE, O_ACCEPT, O_COUNT, O_DENY, O_REJECT,
	O_UNREACH6, O_SKIPTO, O_PIPE, O_QUEUE, O_DIVERT, O_TEE,
	O_NETGRAPH, O_NGTEE, O_FORWARD_IP, O_FORWARD_IP6, O_NAT,
	O_SETFIB, O_SETDSCP, O_REASS, O_CALLRETURN,
	/* keep the following opcodes at the end of the list */
	O_EXTERNAL_ACTION, O_EXTERNAL_INSTANCE, O_EXTERNAL_DATA
};

static const int modifier_opcodes[] = {
	O_LOG, O_ALTQ, O_TAG
};

static const int src_opcodes[] = {
	O_IP_SRC, O_IP_SRC_LOOKUP, O_IP_SRC_MASK, O_IP_SRC_ME,
	O_IP_SRC_SET, O_IP6_SRC, O_IP6_SRC_MASK, O_IP6_SRC_ME
};

static const int dst_opcodes[] = {
	O_IP_DST, O_IP_DST_LOOKUP, O_IP_DST_MASK, O_IP_DST_ME,
	O_IP_DST_SET, O_IP6_DST, O_IP6_DST_MASK, O_IP6_DST_ME
};

static void
show_static_rule(struct cmdline_opts *co, struct format_opts *fo,
    struct buf_pr *bp, struct ip_fw_rule *rule, struct ip_fw_bcounter *cntr)
{
	struct show_state state;
	ipfw_insn *cmd;
	static int twidth = 0;
	int i;

	/* Print # DISABLED or skip the rule */
	if ((fo->set_mask & (1 << rule->set)) == 0) {
		/* disabled mask */
		if (!co->show_sets)
			return;
		else
			bprintf(bp, "# DISABLED ");
	}
	if (init_show_state(&state, rule) != 0) {
		warn("init_show_state() failed");
		return;
	}
	bprintf(bp, "%05u ", rule->rulenum);

	/* Print counters if enabled */
	if (fo->pcwidth > 0 || fo->bcwidth > 0) {
		pr_u64(bp, &cntr->pcnt, fo->pcwidth);
		pr_u64(bp, &cntr->bcnt, fo->bcwidth);
	}

	/* Print timestamp */
	if (co->do_time == TIMESTAMP_NUMERIC)
		bprintf(bp, "%10u ", cntr->timestamp);
	else if (co->do_time == TIMESTAMP_STRING) {
		char timestr[30];
		time_t t = (time_t)0;

		if (twidth == 0) {
			strcpy(timestr, ctime(&t));
			*strchr(timestr, '\n') = '\0';
			twidth = strlen(timestr);
		}
		if (cntr->timestamp > 0) {
			t = _long_to_time(cntr->timestamp);

			strcpy(timestr, ctime(&t));
			*strchr(timestr, '\n') = '\0';
			bprintf(bp, "%s ", timestr);
		} else {
			bprintf(bp, "%*s", twidth, " ");
		}
	}

	/* Print set number */
	if (co->show_sets)
		bprintf(bp, "set %d ", rule->set);

	/* Print the optional "match probability" */
	cmd = print_opcode(bp, fo, &state, O_PROB);
	/* Print rule action */
	for (i = 0; i < nitems(action_opcodes); i++) {
		cmd = print_action(bp, fo, &state, action_opcodes[i]);
		if (cmd == NULL)
			continue;
		/* Handle special cases */
		switch (cmd->opcode) {
		case O_CHECK_STATE:
			goto end;
		case O_EXTERNAL_ACTION:
		case O_EXTERNAL_INSTANCE:
			/* External action can have several instructions */
			continue;
		}
		break;
	}
	/* Print rule modifiers */
	for (i = 0; i < nitems(modifier_opcodes); i++)
		print_action(bp, fo, &state, modifier_opcodes[i]);
	/*
	 * Print rule body
	 */
	if (co->comment_only != 0)
		goto end;

	if (rule->flags & IPFW_RULE_JUSTOPTS) {
		state.flags |= HAVE_PROTO | HAVE_SRCIP | HAVE_DSTIP;
		goto justopts;
	}

	print_proto(bp, fo, &state);

	/* Print source */
	bprintf(bp, " from");
	print_address(bp, fo, &state, src_opcodes, nitems(src_opcodes),
	    O_IP_SRCPORT, HAVE_SRCIP);

	/* Print destination */
	bprintf(bp, " to");
	print_address(bp, fo, &state, dst_opcodes, nitems(dst_opcodes),
	    O_IP_DSTPORT, HAVE_DSTIP);

justopts:
	/* Print the rest of options */
	while (print_opcode(bp, fo, &state, -1))
		;
end:
	/* Print comment at the end */
	cmd = print_opcode(bp, fo, &state, O_NOP);
	if (co->comment_only != 0 && cmd == NULL)
		bprintf(bp, " // ...");
	bprintf(bp, "\n");
	free_show_state(&state);
}

static void
show_dyn_state(struct cmdline_opts *co, struct format_opts *fo,
    struct buf_pr *bp, ipfw_dyn_rule *d)
{
	struct protoent *pe;
	struct in_addr a;
	uint16_t rulenum;
	char buf[INET6_ADDRSTRLEN];

	if (d->expire == 0 && d->dyn_type != O_LIMIT_PARENT)
		return;

	bcopy(&d->rule, &rulenum, sizeof(rulenum));
	bprintf(bp, "%05d", rulenum);
	if (fo->pcwidth > 0 || fo->bcwidth > 0) {
		bprintf(bp, " ");
		pr_u64(bp, &d->pcnt, fo->pcwidth);
		pr_u64(bp, &d->bcnt, fo->bcwidth);
		bprintf(bp, "(%ds)", d->expire);
	}
	switch (d->dyn_type) {
	case O_LIMIT_PARENT:
		bprintf(bp, " PARENT %d", d->count);
		break;
	case O_LIMIT:
		bprintf(bp, " LIMIT");
		break;
	case O_KEEP_STATE: /* bidir, no mask */
		bprintf(bp, " STATE");
		break;
	}

	if ((pe = getprotobynumber(d->id.proto)) != NULL)
		bprintf(bp, " %s", pe->p_name);
	else
		bprintf(bp, " proto %u", d->id.proto);

	if (d->id.addr_type == 4) {
		a.s_addr = htonl(d->id.src_ip);
		bprintf(bp, " %s %d", inet_ntoa(a), d->id.src_port);

		a.s_addr = htonl(d->id.dst_ip);
		bprintf(bp, " <-> %s %d", inet_ntoa(a), d->id.dst_port);
	} else if (d->id.addr_type == 6) {
		bprintf(bp, " %s %d", inet_ntop(AF_INET6, &d->id.src_ip6, buf,
		    sizeof(buf)), d->id.src_port);
		bprintf(bp, " <-> %s %d", inet_ntop(AF_INET6, &d->id.dst_ip6,
		    buf, sizeof(buf)), d->id.dst_port);
	} else
		bprintf(bp, " UNKNOWN <-> UNKNOWN");
	if (d->kidx != 0)
		bprintf(bp, " :%s", object_search_ctlv(fo->tstate,
		    d->kidx, IPFW_TLV_STATE_NAME));

#define	BOTH_SYN	(TH_SYN | (TH_SYN << 8))
#define	BOTH_FIN	(TH_FIN | (TH_FIN << 8))
	if (co->verbose) {
		bprintf(bp, " state 0x%08x%s", d->state,
		    d->state ? " ": ",");
		if (d->state & IPFW_DYN_ORPHANED)
			bprintf(bp, "ORPHANED,");
		if ((d->state & BOTH_SYN) == BOTH_SYN)
			bprintf(bp, "BOTH_SYN,");
		else {
			if (d->state & TH_SYN)
				bprintf(bp, "F_SYN,");
			if (d->state & (TH_SYN << 8))
				bprintf(bp, "R_SYN,");
		}
		if ((d->state & BOTH_FIN) == BOTH_FIN)
			bprintf(bp, "BOTH_FIN,");
		else {
			if (d->state & TH_FIN)
				bprintf(bp, "F_FIN,");
			if (d->state & (TH_FIN << 8))
				bprintf(bp, "R_FIN,");
		}
		bprintf(bp, " f_ack 0x%x, r_ack 0x%x", d->ack_fwd,
		    d->ack_rev);
	}
}

static int
do_range_cmd(int cmd, ipfw_range_tlv *rt)
{
	ipfw_range_header rh;
	size_t sz;

	memset(&rh, 0, sizeof(rh));
	memcpy(&rh.range, rt, sizeof(*rt));
	rh.range.head.length = sizeof(*rt);
	rh.range.head.type = IPFW_TLV_RANGE;
	sz = sizeof(rh);

	if (do_get3(cmd, &rh.opheader, &sz) != 0)
		return (-1);
	/* Save number of matched objects */
	rt->new_set = rh.range.new_set;
	return (0);
}

/*
 * This one handles all set-related commands
 * 	ipfw set { show | enable | disable }
 * 	ipfw set swap X Y
 * 	ipfw set move X to Y
 * 	ipfw set move rule X to Y
 */
void
ipfw_sets_handler(char *av[])
{
	ipfw_range_tlv rt;
	char *msg;
	size_t size;
	uint32_t masks[2];
	int i;
	uint16_t rulenum;
	uint8_t cmd;

	av++;
	memset(&rt, 0, sizeof(rt));

	if (av[0] == NULL)
		errx(EX_USAGE, "set needs command");
	if (_substrcmp(*av, "show") == 0) {
		struct format_opts fo;
		ipfw_cfg_lheader *cfg;

		memset(&fo, 0, sizeof(fo));
		if (ipfw_get_config(&co, &fo, &cfg, &size) != 0)
			err(EX_OSERR, "requesting config failed");

		for (i = 0, msg = "disable"; i < RESVD_SET; i++)
			if ((cfg->set_mask & (1<<i)) == 0) {
				printf("%s %d", msg, i);
				msg = "";
			}
		msg = (cfg->set_mask != (uint32_t)-1) ? " enable" : "enable";
		for (i = 0; i < RESVD_SET; i++)
			if ((cfg->set_mask & (1<<i)) != 0) {
				printf("%s %d", msg, i);
				msg = "";
			}
		printf("\n");
		free(cfg);
	} else if (_substrcmp(*av, "swap") == 0) {
		av++;
		if ( av[0] == NULL || av[1] == NULL )
			errx(EX_USAGE, "set swap needs 2 set numbers\n");
		rt.set = atoi(av[0]);
		rt.new_set = atoi(av[1]);
		if (!isdigit(*(av[0])) || rt.set > RESVD_SET)
			errx(EX_DATAERR, "invalid set number %s\n", av[0]);
		if (!isdigit(*(av[1])) || rt.new_set > RESVD_SET)
			errx(EX_DATAERR, "invalid set number %s\n", av[1]);
		i = do_range_cmd(IP_FW_SET_SWAP, &rt);
	} else if (_substrcmp(*av, "move") == 0) {
		av++;
		if (av[0] && _substrcmp(*av, "rule") == 0) {
			rt.flags = IPFW_RCFLAG_RANGE; /* move rules to new set */
			cmd = IP_FW_XMOVE;
			av++;
		} else
			cmd = IP_FW_SET_MOVE; /* Move set to new one */
		if (av[0] == NULL || av[1] == NULL || av[2] == NULL ||
				av[3] != NULL ||  _substrcmp(av[1], "to") != 0)
			errx(EX_USAGE, "syntax: set move [rule] X to Y\n");
		rulenum = atoi(av[0]);
		rt.new_set = atoi(av[2]);
		if (cmd == IP_FW_XMOVE) {
			rt.start_rule = rulenum;
			rt.end_rule = rulenum;
		} else
			rt.set = rulenum;
		rt.new_set = atoi(av[2]);
		if (!isdigit(*(av[0])) || (cmd == 3 && rt.set > RESVD_SET) ||
			(cmd == 2 && rt.start_rule == IPFW_DEFAULT_RULE) )
			errx(EX_DATAERR, "invalid source number %s\n", av[0]);
		if (!isdigit(*(av[2])) || rt.new_set > RESVD_SET)
			errx(EX_DATAERR, "invalid dest. set %s\n", av[1]);
		i = do_range_cmd(cmd, &rt);
		if (i < 0)
			err(EX_OSERR, "failed to move %s",
			    cmd == IP_FW_SET_MOVE ? "set": "rule");
	} else if (_substrcmp(*av, "disable") == 0 ||
		   _substrcmp(*av, "enable") == 0 ) {
		int which = _substrcmp(*av, "enable") == 0 ? 1 : 0;

		av++;
		masks[0] = masks[1] = 0;

		while (av[0]) {
			if (isdigit(**av)) {
				i = atoi(*av);
				if (i < 0 || i > RESVD_SET)
					errx(EX_DATAERR,
					    "invalid set number %d\n", i);
				masks[which] |= (1<<i);
			} else if (_substrcmp(*av, "disable") == 0)
				which = 0;
			else if (_substrcmp(*av, "enable") == 0)
				which = 1;
			else
				errx(EX_DATAERR,
					"invalid set command %s\n", *av);
			av++;
		}
		if ( (masks[0] & masks[1]) != 0 )
			errx(EX_DATAERR,
			    "cannot enable and disable the same set\n");

		rt.set = masks[0];
		rt.new_set = masks[1];
		i = do_range_cmd(IP_FW_SET_ENABLE, &rt);
		if (i)
			warn("set enable/disable: setsockopt(IP_FW_SET_ENABLE)");
	} else
		errx(EX_USAGE, "invalid set command %s\n", *av);
}

void
ipfw_sysctl_handler(char *av[], int which)
{
	av++;

	if (av[0] == NULL) {
		warnx("missing keyword to enable/disable\n");
	} else if (_substrcmp(*av, "firewall") == 0) {
		sysctlbyname("net.inet.ip.fw.enable", NULL, 0,
		    &which, sizeof(which));
		sysctlbyname("net.inet6.ip6.fw.enable", NULL, 0,
		    &which, sizeof(which));
	} else if (_substrcmp(*av, "one_pass") == 0) {
		sysctlbyname("net.inet.ip.fw.one_pass", NULL, 0,
		    &which, sizeof(which));
	} else if (_substrcmp(*av, "debug") == 0) {
		sysctlbyname("net.inet.ip.fw.debug", NULL, 0,
		    &which, sizeof(which));
	} else if (_substrcmp(*av, "verbose") == 0) {
		sysctlbyname("net.inet.ip.fw.verbose", NULL, 0,
		    &which, sizeof(which));
	} else if (_substrcmp(*av, "dyn_keepalive") == 0) {
		sysctlbyname("net.inet.ip.fw.dyn_keepalive", NULL, 0,
		    &which, sizeof(which));
#ifndef NO_ALTQ
	} else if (_substrcmp(*av, "altq") == 0) {
		altq_set_enabled(which);
#endif
	} else {
		warnx("unrecognize enable/disable keyword: %s\n", *av);
	}
}

typedef void state_cb(struct cmdline_opts *co, struct format_opts *fo,
    void *arg, void *state);

static void
prepare_format_dyn(struct cmdline_opts *co, struct format_opts *fo,
    void *arg, void *_state)
{
	ipfw_dyn_rule *d;
	int width;
	uint8_t set;

	d = (ipfw_dyn_rule *)_state;
	/* Count _ALL_ states */
	fo->dcnt++;

	if (fo->show_counters == 0)
		return;

	if (co->use_set) {
		/* skip states from another set */
		bcopy((char *)&d->rule + sizeof(uint16_t), &set,
		    sizeof(uint8_t));
		if (set != co->use_set - 1)
			return;
	}

	width = pr_u64(NULL, &d->pcnt, 0);
	if (width > fo->pcwidth)
		fo->pcwidth = width;

	width = pr_u64(NULL, &d->bcnt, 0);
	if (width > fo->bcwidth)
		fo->bcwidth = width;
}

static int
foreach_state(struct cmdline_opts *co, struct format_opts *fo,
    caddr_t base, size_t sz, state_cb dyn_bc, void *dyn_arg)
{
	int ttype;
	state_cb *fptr;
	void *farg;
	ipfw_obj_tlv *tlv;
	ipfw_obj_ctlv *ctlv;

	fptr = NULL;
	ttype = 0;

	while (sz > 0) {
		ctlv = (ipfw_obj_ctlv *)base;
		switch (ctlv->head.type) {
		case IPFW_TLV_DYNSTATE_LIST:
			base += sizeof(*ctlv);
			sz -= sizeof(*ctlv);
			ttype = IPFW_TLV_DYN_ENT;
			fptr = dyn_bc;
			farg = dyn_arg;
			break;
		default:
			return (sz);
		}

		while (sz > 0) {
			tlv = (ipfw_obj_tlv *)base;
			if (tlv->type != ttype)
				break;

			fptr(co, fo, farg, tlv + 1);
			sz -= tlv->length;
			base += tlv->length;
		}
	}

	return (sz);
}

static void
prepare_format_opts(struct cmdline_opts *co, struct format_opts *fo,
    ipfw_obj_tlv *rtlv, int rcnt, caddr_t dynbase, size_t dynsz)
{
	int bcwidth, pcwidth, width;
	int n;
	struct ip_fw_bcounter *cntr;
	struct ip_fw_rule *r;

	bcwidth = 0;
	pcwidth = 0;
	if (fo->show_counters != 0) {
		for (n = 0; n < rcnt; n++,
		    rtlv = (ipfw_obj_tlv *)((caddr_t)rtlv + rtlv->length)) {
			cntr = (struct ip_fw_bcounter *)(rtlv + 1);
			r = (struct ip_fw_rule *)((caddr_t)cntr + cntr->size);
			/* skip rules from another set */
			if (co->use_set && r->set != co->use_set - 1)
				continue;

			/* packet counter */
			width = pr_u64(NULL, &cntr->pcnt, 0);
			if (width > pcwidth)
				pcwidth = width;

			/* byte counter */
			width = pr_u64(NULL, &cntr->bcnt, 0);
			if (width > bcwidth)
				bcwidth = width;
		}
	}
	fo->bcwidth = bcwidth;
	fo->pcwidth = pcwidth;

	fo->dcnt = 0;
	if (co->do_dynamic && dynsz > 0)
		foreach_state(co, fo, dynbase, dynsz, prepare_format_dyn, NULL);
}

static int
list_static_range(struct cmdline_opts *co, struct format_opts *fo,
    struct buf_pr *bp, ipfw_obj_tlv *rtlv, int rcnt)
{
	int n, seen;
	struct ip_fw_rule *r;
	struct ip_fw_bcounter *cntr;
	int c = 0;

	for (n = seen = 0; n < rcnt; n++,
	    rtlv = (ipfw_obj_tlv *)((caddr_t)rtlv + rtlv->length)) {

		if ((fo->show_counters | fo->show_time) != 0) {
			cntr = (struct ip_fw_bcounter *)(rtlv + 1);
			r = (struct ip_fw_rule *)((caddr_t)cntr + cntr->size);
		} else {
			cntr = NULL;
			r = (struct ip_fw_rule *)(rtlv + 1);
		}
		if (r->rulenum > fo->last)
			break;
		if (co->use_set && r->set != co->use_set - 1)
			continue;
		if (r->rulenum >= fo->first && r->rulenum <= fo->last) {
			show_static_rule(co, fo, bp, r, cntr);
			printf("%s", bp->buf);
			c += rtlv->length;
			bp_flush(bp);
			seen++;
		}
	}

	return (seen);
}

static void
list_dyn_state(struct cmdline_opts *co, struct format_opts *fo,
    void *_arg, void *_state)
{
	uint16_t rulenum;
	uint8_t set;
	ipfw_dyn_rule *d;
	struct buf_pr *bp;

	d = (ipfw_dyn_rule *)_state;
	bp = (struct buf_pr *)_arg;

	bcopy(&d->rule, &rulenum, sizeof(rulenum));
	if (rulenum > fo->last)
		return;
	if (co->use_set) {
		bcopy((char *)&d->rule + sizeof(uint16_t),
		      &set, sizeof(uint8_t));
		if (set != co->use_set - 1)
			return;
	}
	if (rulenum >= fo->first) {
		show_dyn_state(co, fo, bp, d);
		printf("%s\n", bp->buf);
		bp_flush(bp);
	}
}

static int
list_dyn_range(struct cmdline_opts *co, struct format_opts *fo,
    struct buf_pr *bp, caddr_t base, size_t sz)
{

	sz = foreach_state(co, fo, base, sz, list_dyn_state, bp);
	return (sz);
}

void
ipfw_list(int ac, char *av[], int show_counters)
{
	ipfw_cfg_lheader *cfg;
	struct format_opts sfo;
	size_t sz;
	int error;
	int lac;
	char **lav;
	uint32_t rnum;
	char *endptr;

	if (co.test_only) {
		fprintf(stderr, "Testing only, list disabled\n");
		return;
	}
	if (co.do_pipe) {
		dummynet_list(ac, av, show_counters);
		return;
	}

	ac--;
	av++;
	memset(&sfo, 0, sizeof(sfo));

	/* Determine rule range to request */
	if (ac > 0) {
		for (lac = ac, lav = av; lac != 0; lac--) {
			rnum = strtoul(*lav++, &endptr, 10);
			if (sfo.first == 0 || rnum < sfo.first)
				sfo.first = rnum;

			if (*endptr == '-')
				rnum = strtoul(endptr + 1, &endptr, 10);
			if (sfo.last == 0 || rnum > sfo.last)
				sfo.last = rnum;
		}
	}

	/* get configuraion from kernel */
	cfg = NULL;
	sfo.show_counters = show_counters;
	sfo.show_time = co.do_time;
	if (co.do_dynamic != 2)
		sfo.flags |= IPFW_CFG_GET_STATIC;
	if (co.do_dynamic != 0)
		sfo.flags |= IPFW_CFG_GET_STATES;
	if ((sfo.show_counters | sfo.show_time) != 0)
		sfo.flags |= IPFW_CFG_GET_COUNTERS;
	if (ipfw_get_config(&co, &sfo, &cfg, &sz) != 0)
		err(EX_OSERR, "retrieving config failed");

	error = ipfw_show_config(&co, &sfo, cfg, sz, ac, av);

	free(cfg);

	if (error != EX_OK)
		exit(error);
}

static int
ipfw_show_config(struct cmdline_opts *co, struct format_opts *fo,
    ipfw_cfg_lheader *cfg, size_t sz, int ac, char *av[])
{
	caddr_t dynbase;
	size_t dynsz;
	int rcnt;
	int exitval = EX_OK;
	int lac;
	char **lav;
	char *endptr;
	size_t readsz;
	struct buf_pr bp;
	ipfw_obj_ctlv *ctlv, *tstate;
	ipfw_obj_tlv *rbase;

	/*
	 * Handle tablenames TLV first, if any
	 */
	tstate = NULL;
	rbase = NULL;
	dynbase = NULL;
	dynsz = 0;
	readsz = sizeof(*cfg);
	rcnt = 0;

	fo->set_mask = cfg->set_mask;

	ctlv = (ipfw_obj_ctlv *)(cfg + 1);
	if (ctlv->head.type == IPFW_TLV_TBLNAME_LIST) {
		object_sort_ctlv(ctlv);
		fo->tstate = ctlv;
		readsz += ctlv->head.length;
		ctlv = (ipfw_obj_ctlv *)((caddr_t)ctlv + ctlv->head.length);
	}

	if (cfg->flags & IPFW_CFG_GET_STATIC) {
		/* We've requested static rules */
		if (ctlv->head.type == IPFW_TLV_RULE_LIST) {
			rbase = (ipfw_obj_tlv *)(ctlv + 1);
			rcnt = ctlv->count;
			readsz += ctlv->head.length;
			ctlv = (ipfw_obj_ctlv *)((caddr_t)ctlv +
			    ctlv->head.length);
		}
	}

	if ((cfg->flags & IPFW_CFG_GET_STATES) && (readsz != sz))  {
		/* We may have some dynamic states */
		dynsz = sz - readsz;
		/* Skip empty header */
		if (dynsz != sizeof(ipfw_obj_ctlv))
			dynbase = (caddr_t)ctlv;
		else
			dynsz = 0;
	}

	prepare_format_opts(co, fo, rbase, rcnt, dynbase, dynsz);
	bp_alloc(&bp, 4096);

	/* if no rule numbers were specified, list all rules */
	if (ac == 0) {
		fo->first = 0;
		fo->last = IPFW_DEFAULT_RULE;
		if (cfg->flags & IPFW_CFG_GET_STATIC)
			list_static_range(co, fo, &bp, rbase, rcnt);

		if (co->do_dynamic && dynsz > 0) {
			printf("## Dynamic rules (%d %zu):\n", fo->dcnt,
			    dynsz);
			list_dyn_range(co, fo, &bp, dynbase, dynsz);
		}

		bp_free(&bp);
		return (EX_OK);
	}

	/* display specific rules requested on command line */
	for (lac = ac, lav = av; lac != 0; lac--) {
		/* convert command line rule # */
		fo->last = fo->first = strtoul(*lav++, &endptr, 10);
		if (*endptr == '-')
			fo->last = strtoul(endptr + 1, &endptr, 10);
		if (*endptr) {
			exitval = EX_USAGE;
			warnx("invalid rule number: %s", *(lav - 1));
			continue;
		}

		if ((cfg->flags & IPFW_CFG_GET_STATIC) == 0)
			continue;

		if (list_static_range(co, fo, &bp, rbase, rcnt) == 0) {
			/* give precedence to other error(s) */
			if (exitval == EX_OK)
				exitval = EX_UNAVAILABLE;
			if (fo->first == fo->last)
				warnx("rule %u does not exist", fo->first);
			else
				warnx("no rules in range %u-%u",
				    fo->first, fo->last);
		}
	}

	if (co->do_dynamic && dynsz > 0) {
		printf("## Dynamic rules:\n");
		for (lac = ac, lav = av; lac != 0; lac--) {
			fo->last = fo->first = strtoul(*lav++, &endptr, 10);
			if (*endptr == '-')
				fo->last = strtoul(endptr+1, &endptr, 10);
			if (*endptr)
				/* already warned */
				continue;
			list_dyn_range(co, fo, &bp, dynbase, dynsz);
		}
	}

	bp_free(&bp);
	return (exitval);
}


/*
 * Retrieves current ipfw configuration of given type
 * and stores its pointer to @pcfg.
 *
 * Caller is responsible for freeing @pcfg.
 *
 * Returns 0 on success.
 */

static int
ipfw_get_config(struct cmdline_opts *co, struct format_opts *fo,
    ipfw_cfg_lheader **pcfg, size_t *psize)
{
	ipfw_cfg_lheader *cfg;
	size_t sz;
	int i;


	if (co->test_only != 0) {
		fprintf(stderr, "Testing only, list disabled\n");
		return (0);
	}

	/* Start with some data size */
	sz = 4096;
	cfg = NULL;

	for (i = 0; i < 16; i++) {
		if (cfg != NULL)
			free(cfg);
		if ((cfg = calloc(1, sz)) == NULL)
			return (ENOMEM);

		cfg->flags = fo->flags;
		cfg->start_rule = fo->first;
		cfg->end_rule = fo->last;

		if (do_get3(IP_FW_XGET, &cfg->opheader, &sz) != 0) {
			if (errno != ENOMEM) {
				free(cfg);
				return (errno);
			}

			/* Buffer size is not enough. Try to increase */
			sz = sz * 2;
			if (sz < cfg->size)
				sz = cfg->size;
			continue;
		}

		*pcfg = cfg;
		*psize = sz;
		return (0);
	}

	free(cfg);
	return (ENOMEM);
}

static int
lookup_host (char *host, struct in_addr *ipaddr)
{
	struct hostent *he;

	if (!inet_aton(host, ipaddr)) {
		if ((he = gethostbyname(host)) == NULL)
			return(-1);
		*ipaddr = *(struct in_addr *)he->h_addr_list[0];
	}
	return(0);
}

struct tidx {
	ipfw_obj_ntlv *idx;
	uint32_t count;
	uint32_t size;
	uint16_t counter;
	uint8_t set;
};

int
ipfw_check_object_name(const char *name)
{
	int c, i, l;

	/*
	 * Check that name is null-terminated and contains
	 * valid symbols only. Valid mask is:
	 * [a-zA-Z0-9\-_\.]{1,63}
	 */
	l = strlen(name);
	if (l == 0 || l >= 64)
		return (EINVAL);
	for (i = 0; i < l; i++) {
		c = name[i];
		if (isalpha(c) || isdigit(c) || c == '_' ||
		    c == '-' || c == '.')
			continue;
		return (EINVAL);
	}
	return (0);
}

static char *default_state_name = "default";
static int
state_check_name(const char *name)
{

	if (ipfw_check_object_name(name) != 0)
		return (EINVAL);
	if (strcmp(name, "any") == 0)
		return (EINVAL);
	return (0);
}

static int
eaction_check_name(const char *name)
{

	if (ipfw_check_object_name(name) != 0)
		return (EINVAL);
	/* Restrict some 'special' names */
	if (match_token(rule_actions, name) != -1 &&
	    match_token(rule_action_params, name) != -1)
		return (EINVAL);
	return (0);
}

static uint16_t
pack_object(struct tidx *tstate, char *name, int otype)
{
	int i;
	ipfw_obj_ntlv *ntlv;

	for (i = 0; i < tstate->count; i++) {
		if (strcmp(tstate->idx[i].name, name) != 0)
			continue;
		if (tstate->idx[i].set != tstate->set)
			continue;
		if (tstate->idx[i].head.type != otype)
			continue;

		return (tstate->idx[i].idx);
	}

	if (tstate->count + 1 > tstate->size) {
		tstate->size += 4;
		tstate->idx = realloc(tstate->idx, tstate->size *
		    sizeof(ipfw_obj_ntlv));
		if (tstate->idx == NULL)
			return (0);
	}

	ntlv = &tstate->idx[i];
	memset(ntlv, 0, sizeof(ipfw_obj_ntlv));
	strlcpy(ntlv->name, name, sizeof(ntlv->name));
	ntlv->head.type = otype;
	ntlv->head.length = sizeof(ipfw_obj_ntlv);
	ntlv->set = tstate->set;
	ntlv->idx = ++tstate->counter;
	tstate->count++;

	return (ntlv->idx);
}

static uint16_t
pack_table(struct tidx *tstate, char *name)
{

	if (table_check_name(name) != 0)
		return (0);

	return (pack_object(tstate, name, IPFW_TLV_TBL_NAME));
}

void
fill_table(struct _ipfw_insn *cmd, char *av, uint8_t opcode,
    struct tidx *tstate)
{
	uint32_t *d = ((ipfw_insn_u32 *)cmd)->d;
	uint16_t uidx;
	char *p;

	if ((p = strchr(av + 6, ')')) == NULL)
		errx(EX_DATAERR, "forgotten parenthesis: '%s'", av);
	*p = '\0';
	p = strchr(av + 6, ',');
	if (p)
		*p++ = '\0';

	if ((uidx = pack_table(tstate, av + 6)) == 0)
		errx(EX_DATAERR, "Invalid table name: %s", av + 6);

	cmd->opcode = opcode;
	cmd->arg1 = uidx;
	if (p) {
		cmd->len |= F_INSN_SIZE(ipfw_insn_u32);
		d[0] = strtoul(p, NULL, 0);
	} else
		cmd->len |= F_INSN_SIZE(ipfw_insn);
}


/*
 * fills the addr and mask fields in the instruction as appropriate from av.
 * Update length as appropriate.
 * The following formats are allowed:
 *	me	returns O_IP_*_ME
 *	1.2.3.4		single IP address
 *	1.2.3.4:5.6.7.8	address:mask
 *	1.2.3.4/24	address/mask
 *	1.2.3.4/26{1,6,5,4,23}	set of addresses in a subnet
 * We can have multiple comma-separated address/mask entries.
 */
static void
fill_ip(ipfw_insn_ip *cmd, char *av, int cblen, struct tidx *tstate)
{
	int len = 0;
	uint32_t *d = ((ipfw_insn_u32 *)cmd)->d;

	cmd->o.len &= ~F_LEN_MASK;	/* zero len */

	if (_substrcmp(av, "any") == 0)
		return;

	if (_substrcmp(av, "me") == 0) {
		cmd->o.len |= F_INSN_SIZE(ipfw_insn);
		return;
	}

	if (strncmp(av, "table(", 6) == 0) {
		fill_table(&cmd->o, av, O_IP_DST_LOOKUP, tstate);
		return;
	}

    while (av) {
	/*
	 * After the address we can have '/' or ':' indicating a mask,
	 * ',' indicating another address follows, '{' indicating a
	 * set of addresses of unspecified size.
	 */
	char *t = NULL, *p = strpbrk(av, "/:,{");
	int masklen;
	char md, nd = '\0';

	CHECK_LENGTH(cblen, F_INSN_SIZE(ipfw_insn) + 2 + len);

	if (p) {
		md = *p;
		*p++ = '\0';
		if ((t = strpbrk(p, ",{")) != NULL) {
			nd = *t;
			*t = '\0';
		}
	} else
		md = '\0';

	if (lookup_host(av, (struct in_addr *)&d[0]) != 0)
		errx(EX_NOHOST, "hostname ``%s'' unknown", av);
	switch (md) {
	case ':':
		if (!inet_aton(p, (struct in_addr *)&d[1]))
			errx(EX_DATAERR, "bad netmask ``%s''", p);
		break;
	case '/':
		masklen = atoi(p);
		if (masklen == 0)
			d[1] = htonl(0U);	/* mask */
		else if (masklen > 32)
			errx(EX_DATAERR, "bad width ``%s''", p);
		else
			d[1] = htonl(~0U << (32 - masklen));
		break;
	case '{':	/* no mask, assume /24 and put back the '{' */
		d[1] = htonl(~0U << (32 - 24));
		*(--p) = md;
		break;

	case ',':	/* single address plus continuation */
		*(--p) = md;
		/* FALLTHROUGH */
	case 0:		/* initialization value */
	default:
		d[1] = htonl(~0U);	/* force /32 */
		break;
	}
	d[0] &= d[1];		/* mask base address with mask */
	if (t)
		*t = nd;
	/* find next separator */
	if (p)
		p = strpbrk(p, ",{");
	if (p && *p == '{') {
		/*
		 * We have a set of addresses. They are stored as follows:
		 *   arg1	is the set size (powers of 2, 2..256)
		 *   addr	is the base address IN HOST FORMAT
		 *   mask..	is an array of arg1 bits (rounded up to
		 *		the next multiple of 32) with bits set
		 *		for each host in the map.
		 */
		uint32_t *map = (uint32_t *)&cmd->mask;
		int low, high;
		int i = contigmask((uint8_t *)&(d[1]), 32);

		if (len > 0)
			errx(EX_DATAERR, "address set cannot be in a list");
		if (i < 24 || i > 31)
			errx(EX_DATAERR, "invalid set with mask %d\n", i);
		cmd->o.arg1 = 1<<(32-i);	/* map length		*/
		d[0] = ntohl(d[0]);		/* base addr in host format */
		cmd->o.opcode = O_IP_DST_SET;	/* default */
		cmd->o.len |= F_INSN_SIZE(ipfw_insn_u32) + (cmd->o.arg1+31)/32;
		for (i = 0; i < (cmd->o.arg1+31)/32 ; i++)
			map[i] = 0;	/* clear map */

		av = p + 1;
		low = d[0] & 0xff;
		high = low + cmd->o.arg1 - 1;
		/*
		 * Here, i stores the previous value when we specify a range
		 * of addresses within a mask, e.g. 45-63. i = -1 means we
		 * have no previous value.
		 */
		i = -1;	/* previous value in a range */
		while (isdigit(*av)) {
			char *s;
			int a = strtol(av, &s, 0);

			if (s == av) { /* no parameter */
			    if (*av != '}')
				errx(EX_DATAERR, "set not closed\n");
			    if (i != -1)
				errx(EX_DATAERR, "incomplete range %d-", i);
			    break;
			}
			if (a < low || a > high)
			    errx(EX_DATAERR, "addr %d out of range [%d-%d]\n",
				a, low, high);
			a -= low;
			if (i == -1)	/* no previous in range */
			    i = a;
			else {		/* check that range is valid */
			    if (i > a)
				errx(EX_DATAERR, "invalid range %d-%d",
					i+low, a+low);
			    if (*s == '-')
				errx(EX_DATAERR, "double '-' in range");
			}
			for (; i <= a; i++)
			    map[i/32] |= 1<<(i & 31);
			i = -1;
			if (*s == '-')
			    i = a;
			else if (*s == '}')
			    break;
			av = s+1;
		}
		return;
	}
	av = p;
	if (av)			/* then *av must be a ',' */
		av++;

	/* Check this entry */
	if (d[1] == 0) { /* "any", specified as x.x.x.x/0 */
		/*
		 * 'any' turns the entire list into a NOP.
		 * 'not any' never matches, so it is removed from the
		 * list unless it is the only item, in which case we
		 * report an error.
		 */
		if (cmd->o.len & F_NOT) {	/* "not any" never matches */
			if (av == NULL && len == 0) /* only this entry */
				errx(EX_DATAERR, "not any never matches");
		}
		/* else do nothing and skip this entry */
		return;
	}
	/* A single IP can be stored in an optimized format */
	if (d[1] == (uint32_t)~0 && av == NULL && len == 0) {
		cmd->o.len |= F_INSN_SIZE(ipfw_insn_u32);
		return;
	}
	len += 2;	/* two words... */
	d += 2;
    } /* end while */
    if (len + 1 > F_LEN_MASK)
	errx(EX_DATAERR, "address list too long");
    cmd->o.len |= len+1;
}


/* n2mask sets n bits of the mask */
void
n2mask(struct in6_addr *mask, int n)
{
	static int	minimask[9] =
	    { 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff };
	u_char		*p;

	memset(mask, 0, sizeof(struct in6_addr));
	p = (u_char *) mask;
	for (; n > 0; p++, n -= 8) {
		if (n >= 8)
			*p = 0xff;
		else
			*p = minimask[n];
	}
	return;
}

static void
fill_flags_cmd(ipfw_insn *cmd, enum ipfw_opcodes opcode,
	struct _s_x *flags, char *p)
{
	char *e;
	uint32_t set = 0, clear = 0;

	if (fill_flags(flags, p, &e, &set, &clear) != 0)
		errx(EX_DATAERR, "invalid flag %s", e);

	cmd->opcode = opcode;
	cmd->len =  (cmd->len & (F_NOT | F_OR)) | 1;
	cmd->arg1 = (set & 0xff) | ( (clear & 0xff) << 8);
}


void
ipfw_delete(char *av[])
{
	ipfw_range_tlv rt;
	char *sep;
	int i, j;
	int exitval = EX_OK;
	int do_set = 0;

	av++;
	NEED1("missing rule specification");
	if ( *av && _substrcmp(*av, "set") == 0) {
		/* Do not allow using the following syntax:
		 *	ipfw set N delete set M
		 */
		if (co.use_set)
			errx(EX_DATAERR, "invalid syntax");
		do_set = 1;	/* delete set */
		av++;
	}

	/* Rule number */
	while (*av && isdigit(**av)) {
		i = strtol(*av, &sep, 10);
		j = i;
		if (*sep== '-')
			j = strtol(sep + 1, NULL, 10);
		av++;
		if (co.do_nat) {
			exitval = do_cmd(IP_FW_NAT_DEL, &i, sizeof i);
			if (exitval) {
				exitval = EX_UNAVAILABLE;
				if (co.do_quiet)
					continue;
				warn("nat %u not available", i);
			}
		} else if (co.do_pipe) {
			exitval = ipfw_delete_pipe(co.do_pipe, i);
		} else {
			memset(&rt, 0, sizeof(rt));
			if (do_set != 0) {
				rt.set = i & 31;
				rt.flags = IPFW_RCFLAG_SET;
			} else {
				rt.start_rule = i & 0xffff;
				rt.end_rule = j & 0xffff;
				if (rt.start_rule == 0 && rt.end_rule == 0)
					rt.flags |= IPFW_RCFLAG_ALL;
				else
					rt.flags |= IPFW_RCFLAG_RANGE;
				if (co.use_set != 0) {
					rt.set = co.use_set - 1;
					rt.flags |= IPFW_RCFLAG_SET;
				}
			}
			if (co.do_dynamic == 2)
				rt.flags |= IPFW_RCFLAG_DYNAMIC;
			i = do_range_cmd(IP_FW_XDEL, &rt);
			if (i != 0) {
				exitval = EX_UNAVAILABLE;
				if (co.do_quiet)
					continue;
				warn("rule %u: setsockopt(IP_FW_XDEL)",
				    rt.start_rule);
			} else if (rt.new_set == 0 && do_set == 0 &&
			    co.do_dynamic != 2) {
				exitval = EX_UNAVAILABLE;
				if (co.do_quiet)
					continue;
				if (rt.start_rule != rt.end_rule)
					warnx("no rules rules in %u-%u range",
					    rt.start_rule, rt.end_rule);
				else
					warnx("rule %u not found",
					    rt.start_rule);
			}
		}
	}
	if (exitval != EX_OK && co.do_force == 0)
		exit(exitval);
}


/*
 * fill the interface structure. We do not check the name as we can
 * create interfaces dynamically, so checking them at insert time
 * makes relatively little sense.
 * Interface names containing '*', '?', or '[' are assumed to be shell
 * patterns which match interfaces.
 */
static void
fill_iface(ipfw_insn_if *cmd, char *arg, int cblen, struct tidx *tstate)
{
	char *p;
	uint16_t uidx;

	cmd->name[0] = '\0';
	cmd->o.len |= F_INSN_SIZE(ipfw_insn_if);

	CHECK_CMDLEN;

	/* Parse the interface or address */
	if (strcmp(arg, "any") == 0)
		cmd->o.len = 0;		/* effectively ignore this command */
	else if (strncmp(arg, "table(", 6) == 0) {
		if ((p = strchr(arg + 6, ')')) == NULL)
			errx(EX_DATAERR, "forgotten parenthesis: '%s'", arg);
		*p = '\0';
		p = strchr(arg + 6, ',');
		if (p)
			*p++ = '\0';
		if ((uidx = pack_table(tstate, arg + 6)) == 0)
			errx(EX_DATAERR, "Invalid table name: %s", arg + 6);

		cmd->name[0] = '\1'; /* Special value indicating table */
		cmd->p.kidx = uidx;
	} else if (!isdigit(*arg)) {
		strlcpy(cmd->name, arg, sizeof(cmd->name));
		cmd->p.glob = strpbrk(arg, "*?[") != NULL ? 1 : 0;
	} else if (!inet_aton(arg, &cmd->p.ip))
		errx(EX_DATAERR, "bad ip address ``%s''", arg);
}

static void
get_mac_addr_mask(const char *p, uint8_t *addr, uint8_t *mask)
{
	int i;
	size_t l;
	char *ap, *ptr, *optr;
	struct ether_addr *mac;
	const char *macset = "0123456789abcdefABCDEF:";

	if (strcmp(p, "any") == 0) {
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			addr[i] = mask[i] = 0;
		return;
	}

	optr = ptr = strdup(p);
	if ((ap = strsep(&ptr, "&/")) != NULL && *ap != 0) {
		l = strlen(ap);
		if (strspn(ap, macset) != l || (mac = ether_aton(ap)) == NULL)
			errx(EX_DATAERR, "Incorrect MAC address");
		bcopy(mac, addr, ETHER_ADDR_LEN);
	} else
		errx(EX_DATAERR, "Incorrect MAC address");

	if (ptr != NULL) { /* we have mask? */
		if (p[ptr - optr - 1] == '/') { /* mask len */
			long ml = strtol(ptr, &ap, 10);
			if (*ap != 0 || ml > ETHER_ADDR_LEN * 8 || ml < 0)
				errx(EX_DATAERR, "Incorrect mask length");
			for (i = 0; ml > 0 && i < ETHER_ADDR_LEN; ml -= 8, i++)
				mask[i] = (ml >= 8) ? 0xff: (~0) << (8 - ml);
		} else { /* mask */
			l = strlen(ptr);
			if (strspn(ptr, macset) != l ||
			    (mac = ether_aton(ptr)) == NULL)
				errx(EX_DATAERR, "Incorrect mask");
			bcopy(mac, mask, ETHER_ADDR_LEN);
		}
	} else { /* default mask: ff:ff:ff:ff:ff:ff */
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			mask[i] = 0xff;
	}
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		addr[i] &= mask[i];

	free(optr);
}

/*
 * helper function, updates the pointer to cmd with the length
 * of the current command, and also cleans up the first word of
 * the new command in case it has been clobbered before.
 */
static ipfw_insn *
next_cmd(ipfw_insn *cmd, int *len)
{
	*len -= F_LEN(cmd);
	CHECK_LENGTH(*len, 0);
	cmd += F_LEN(cmd);
	bzero(cmd, sizeof(*cmd));
	return cmd;
}

/*
 * Takes arguments and copies them into a comment
 */
static void
fill_comment(ipfw_insn *cmd, char **av, int cblen)
{
	int i, l;
	char *p = (char *)(cmd + 1);

	cmd->opcode = O_NOP;
	cmd->len =  (cmd->len & (F_NOT | F_OR));

	/* Compute length of comment string. */
	for (i = 0, l = 0; av[i] != NULL; i++)
		l += strlen(av[i]) + 1;
	if (l == 0)
		return;
	if (l > 84)
		errx(EX_DATAERR,
		    "comment too long (max 80 chars)");
	l = 1 + (l+3)/4;
	cmd->len =  (cmd->len & (F_NOT | F_OR)) | l;
	CHECK_CMDLEN;

	for (i = 0; av[i] != NULL; i++) {
		strcpy(p, av[i]);
		p += strlen(av[i]);
		*p++ = ' ';
	}
	*(--p) = '\0';
}

/*
 * A function to fill simple commands of size 1.
 * Existing flags are preserved.
 */
static void
fill_cmd(ipfw_insn *cmd, enum ipfw_opcodes opcode, int flags, uint16_t arg)
{
	cmd->opcode = opcode;
	cmd->len =  ((cmd->len | flags) & (F_NOT | F_OR)) | 1;
	cmd->arg1 = arg;
}

/*
 * Fetch and add the MAC address and type, with masks. This generates one or
 * two microinstructions, and returns the pointer to the last one.
 */
static ipfw_insn *
add_mac(ipfw_insn *cmd, char *av[], int cblen)
{
	ipfw_insn_mac *mac;

	if ( ( av[0] == NULL ) || ( av[1] == NULL ) )
		errx(EX_DATAERR, "MAC dst src");

	cmd->opcode = O_MACADDR2;
	cmd->len = (cmd->len & (F_NOT | F_OR)) | F_INSN_SIZE(ipfw_insn_mac);
	CHECK_CMDLEN;

	mac = (ipfw_insn_mac *)cmd;
	get_mac_addr_mask(av[0], mac->addr, mac->mask);	/* dst */
	get_mac_addr_mask(av[1], &(mac->addr[ETHER_ADDR_LEN]),
	    &(mac->mask[ETHER_ADDR_LEN])); /* src */
	return cmd;
}

static ipfw_insn *
add_mactype(ipfw_insn *cmd, char *av, int cblen)
{
	if (!av)
		errx(EX_DATAERR, "missing MAC type");
	if (strcmp(av, "any") != 0) { /* we have a non-null type */
		fill_newports((ipfw_insn_u16 *)cmd, av, IPPROTO_ETHERTYPE,
		    cblen);
		cmd->opcode = O_MAC_TYPE;
		return cmd;
	} else
		return NULL;
}

static ipfw_insn *
add_proto0(ipfw_insn *cmd, char *av, u_char *protop)
{
	struct protoent *pe;
	char *ep;
	int proto;

	proto = strtol(av, &ep, 10);
	if (*ep != '\0' || proto <= 0) {
		if ((pe = getprotobyname(av)) == NULL)
			return NULL;
		proto = pe->p_proto;
	}

	fill_cmd(cmd, O_PROTO, 0, proto);
	*protop = proto;
	return cmd;
}

static ipfw_insn *
add_proto(ipfw_insn *cmd, char *av, u_char *protop)
{
	u_char proto = IPPROTO_IP;

	if (_substrcmp(av, "all") == 0 || strcmp(av, "ip") == 0)
		; /* do not set O_IP4 nor O_IP6 */
	else if (strcmp(av, "ip4") == 0)
		/* explicit "just IPv4" rule */
		fill_cmd(cmd, O_IP4, 0, 0);
	else if (strcmp(av, "ip6") == 0) {
		/* explicit "just IPv6" rule */
		proto = IPPROTO_IPV6;
		fill_cmd(cmd, O_IP6, 0, 0);
	} else
		return add_proto0(cmd, av, protop);

	*protop = proto;
	return cmd;
}

static ipfw_insn *
add_proto_compat(ipfw_insn *cmd, char *av, u_char *protop)
{
	u_char proto = IPPROTO_IP;

	if (_substrcmp(av, "all") == 0 || strcmp(av, "ip") == 0)
		; /* do not set O_IP4 nor O_IP6 */
	else if (strcmp(av, "ipv4") == 0 || strcmp(av, "ip4") == 0)
		/* explicit "just IPv4" rule */
		fill_cmd(cmd, O_IP4, 0, 0);
	else if (strcmp(av, "ipv6") == 0 || strcmp(av, "ip6") == 0) {
		/* explicit "just IPv6" rule */
		proto = IPPROTO_IPV6;
		fill_cmd(cmd, O_IP6, 0, 0);
	} else
		return add_proto0(cmd, av, protop);

	*protop = proto;
	return cmd;
}

static ipfw_insn *
add_srcip(ipfw_insn *cmd, char *av, int cblen, struct tidx *tstate)
{
	fill_ip((ipfw_insn_ip *)cmd, av, cblen, tstate);
	if (cmd->opcode == O_IP_DST_SET)			/* set */
		cmd->opcode = O_IP_SRC_SET;
	else if (cmd->opcode == O_IP_DST_LOOKUP)		/* table */
		cmd->opcode = O_IP_SRC_LOOKUP;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn))		/* me */
		cmd->opcode = O_IP_SRC_ME;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_u32))	/* one IP */
		cmd->opcode = O_IP_SRC;
	else							/* addr/mask */
		cmd->opcode = O_IP_SRC_MASK;
	return cmd;
}

static ipfw_insn *
add_dstip(ipfw_insn *cmd, char *av, int cblen, struct tidx *tstate)
{
	fill_ip((ipfw_insn_ip *)cmd, av, cblen, tstate);
	if (cmd->opcode == O_IP_DST_SET)			/* set */
		;
	else if (cmd->opcode == O_IP_DST_LOOKUP)		/* table */
		;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn))		/* me */
		cmd->opcode = O_IP_DST_ME;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_u32))	/* one IP */
		cmd->opcode = O_IP_DST;
	else							/* addr/mask */
		cmd->opcode = O_IP_DST_MASK;
	return cmd;
}

static struct _s_x f_reserved_keywords[] = {
	{ "altq",	TOK_OR },
	{ "//",		TOK_OR },
	{ "diverted",	TOK_OR },
	{ "dst-port",	TOK_OR },
	{ "src-port",	TOK_OR },
	{ "established",	TOK_OR },
	{ "keep-state",	TOK_OR },
	{ "frag",	TOK_OR },
	{ "icmptypes",	TOK_OR },
	{ "in",		TOK_OR },
	{ "out",	TOK_OR },
	{ "ip6",	TOK_OR },
	{ "any",	TOK_OR },
	{ "to",		TOK_OR },
	{ "via",	TOK_OR },
	{ "{",		TOK_OR },
	{ NULL, 0 }	/* terminator */
};

static ipfw_insn *
add_ports(ipfw_insn *cmd, char *av, u_char proto, int opcode, int cblen)
{

	if (match_token(f_reserved_keywords, av) != -1)
		return (NULL);

	if (fill_newports((ipfw_insn_u16 *)cmd, av, proto, cblen)) {
		/* XXX todo: check that we have a protocol with ports */
		cmd->opcode = opcode;
		return cmd;
	}
	return NULL;
}

static ipfw_insn *
add_src(ipfw_insn *cmd, char *av, u_char proto, int cblen, struct tidx *tstate)
{
	struct in6_addr a;
	char *host, *ch, buf[INET6_ADDRSTRLEN];
	ipfw_insn *ret = NULL;
	int len;

	/* Copy first address in set if needed */
	if ((ch = strpbrk(av, "/,")) != NULL) {
		len = ch - av;
		strlcpy(buf, av, sizeof(buf));
		if (len < sizeof(buf))
			buf[len] = '\0';
		host = buf;
	} else
		host = av;

	if (proto == IPPROTO_IPV6  || strcmp(av, "me6") == 0 ||
	    inet_pton(AF_INET6, host, &a) == 1)
		ret = add_srcip6(cmd, av, cblen, tstate);
	/* XXX: should check for IPv4, not !IPv6 */
	if (ret == NULL && (proto == IPPROTO_IP || strcmp(av, "me") == 0 ||
	    inet_pton(AF_INET6, host, &a) != 1))
		ret = add_srcip(cmd, av, cblen, tstate);
	if (ret == NULL && strcmp(av, "any") != 0)
		ret = cmd;

	return ret;
}

static ipfw_insn *
add_dst(ipfw_insn *cmd, char *av, u_char proto, int cblen, struct tidx *tstate)
{
	struct in6_addr a;
	char *host, *ch, buf[INET6_ADDRSTRLEN];
	ipfw_insn *ret = NULL;
	int len;

	/* Copy first address in set if needed */
	if ((ch = strpbrk(av, "/,")) != NULL) {
		len = ch - av;
		strlcpy(buf, av, sizeof(buf));
		if (len < sizeof(buf))
			buf[len] = '\0';
		host = buf;
	} else
		host = av;

	if (proto == IPPROTO_IPV6  || strcmp(av, "me6") == 0 ||
	    inet_pton(AF_INET6, host, &a) == 1)
		ret = add_dstip6(cmd, av, cblen, tstate);
	/* XXX: should check for IPv4, not !IPv6 */
	if (ret == NULL && (proto == IPPROTO_IP || strcmp(av, "me") == 0 ||
	    inet_pton(AF_INET6, host, &a) != 1))
		ret = add_dstip(cmd, av, cblen, tstate);
	if (ret == NULL && strcmp(av, "any") != 0)
		ret = cmd;

	return ret;
}

/*
 * Parse arguments and assemble the microinstructions which make up a rule.
 * Rules are added into the 'rulebuf' and then copied in the correct order
 * into the actual rule.
 *
 * The syntax for a rule starts with the action, followed by
 * optional action parameters, and the various match patterns.
 * In the assembled microcode, the first opcode must be an O_PROBE_STATE
 * (generated if the rule includes a keep-state option), then the
 * various match patterns, log/altq actions, and the actual action.
 *
 */
void
compile_rule(char *av[], uint32_t *rbuf, int *rbufsize, struct tidx *tstate)
{
	/*
	 * rules are added into the 'rulebuf' and then copied in
	 * the correct order into the actual rule.
	 * Some things that need to go out of order (prob, action etc.)
	 * go into actbuf[].
	 */
	static uint32_t actbuf[255], cmdbuf[255];
	int rblen, ablen, cblen;

	ipfw_insn *src, *dst, *cmd, *action, *prev=NULL;
	ipfw_insn *first_cmd;	/* first match pattern */

	struct ip_fw_rule *rule;

	/*
	 * various flags used to record that we entered some fields.
	 */
	ipfw_insn *have_state = NULL;	/* any state-related option */
	int have_rstate = 0;
	ipfw_insn *have_log = NULL, *have_altq = NULL, *have_tag = NULL;
	ipfw_insn *have_skipcmd = NULL;
	size_t len;

	int i;

	int open_par = 0;	/* open parenthesis ( */

	/* proto is here because it is used to fetch ports */
	u_char proto = IPPROTO_IP;	/* default protocol */

	double match_prob = 1; /* match probability, default is always match */

	bzero(actbuf, sizeof(actbuf));		/* actions go here */
	bzero(cmdbuf, sizeof(cmdbuf));
	bzero(rbuf, *rbufsize);

	rule = (struct ip_fw_rule *)rbuf;
	cmd = (ipfw_insn *)cmdbuf;
	action = (ipfw_insn *)actbuf;

	rblen = *rbufsize / sizeof(uint32_t);
	rblen -= sizeof(struct ip_fw_rule) / sizeof(uint32_t);
	ablen = sizeof(actbuf) / sizeof(actbuf[0]);
	cblen = sizeof(cmdbuf) / sizeof(cmdbuf[0]);
	cblen -= F_INSN_SIZE(ipfw_insn_u32) + 1;

#define	CHECK_RBUFLEN(len)	{ CHECK_LENGTH(rblen, len); rblen -= len; }
#define	CHECK_ACTLEN		CHECK_LENGTH(ablen, action->len)

	av++;

	/* [rule N]	-- Rule number optional */
	if (av[0] && isdigit(**av)) {
		rule->rulenum = atoi(*av);
		av++;
	}

	/* [set N]	-- set number (0..RESVD_SET), optional */
	if (av[0] && av[1] && _substrcmp(*av, "set") == 0) {
		int set = strtoul(av[1], NULL, 10);
		if (set < 0 || set > RESVD_SET)
			errx(EX_DATAERR, "illegal set %s", av[1]);
		rule->set = set;
		tstate->set = set;
		av += 2;
	}

	/* [prob D]	-- match probability, optional */
	if (av[0] && av[1] && _substrcmp(*av, "prob") == 0) {
		match_prob = strtod(av[1], NULL);

		if (match_prob <= 0 || match_prob > 1)
			errx(EX_DATAERR, "illegal match prob. %s", av[1]);
		av += 2;
	}

	/* action	-- mandatory */
	NEED1("missing action");
	i = match_token(rule_actions, *av);
	av++;
	action->len = 1;	/* default */
	CHECK_ACTLEN;
	switch(i) {
	case TOK_CHECKSTATE:
		have_state = action;
		action->opcode = O_CHECK_STATE;
		if (*av == NULL ||
		    match_token(rule_options, *av) == TOK_COMMENT) {
			action->arg1 = pack_object(tstate,
			    default_state_name, IPFW_TLV_STATE_NAME);
			break;
		}
		if (*av[0] == ':') {
			if (strcmp(*av + 1, "any") == 0)
				action->arg1 = 0;
			else if (state_check_name(*av + 1) == 0)
				action->arg1 = pack_object(tstate, *av + 1,
				    IPFW_TLV_STATE_NAME);
			else
				errx(EX_DATAERR, "Invalid state name %s",
				    *av);
			av++;
			break;
		}
		errx(EX_DATAERR, "Invalid state name %s", *av);
		break;

	case TOK_ABORT:
		action->opcode = O_REJECT;
		action->arg1 = ICMP_REJECT_ABORT;
		break;

	case TOK_ABORT6:
		action->opcode = O_UNREACH6;
		action->arg1 = ICMP6_UNREACH_ABORT;
		break;

	case TOK_ACCEPT:
		action->opcode = O_ACCEPT;
		break;

	case TOK_DENY:
		action->opcode = O_DENY;
		action->arg1 = 0;
		break;

	case TOK_REJECT:
		action->opcode = O_REJECT;
		action->arg1 = ICMP_UNREACH_HOST;
		break;

	case TOK_RESET:
		action->opcode = O_REJECT;
		action->arg1 = ICMP_REJECT_RST;
		break;

	case TOK_RESET6:
		action->opcode = O_UNREACH6;
		action->arg1 = ICMP6_UNREACH_RST;
		break;

	case TOK_UNREACH:
		action->opcode = O_REJECT;
		NEED1("missing reject code");
		fill_reject_code(&action->arg1, *av);
		av++;
		break;

	case TOK_UNREACH6:
		action->opcode = O_UNREACH6;
		NEED1("missing unreach code");
		fill_unreach6_code(&action->arg1, *av);
		av++;
		break;

	case TOK_COUNT:
		action->opcode = O_COUNT;
		break;

	case TOK_NAT:
		action->opcode = O_NAT;
		action->len = F_INSN_SIZE(ipfw_insn_nat);
		CHECK_ACTLEN;
		if (*av != NULL && _substrcmp(*av, "global") == 0) {
			action->arg1 = IP_FW_NAT44_GLOBAL;
			av++;
			break;
		} else
			goto chkarg;
	case TOK_QUEUE:
		action->opcode = O_QUEUE;
		goto chkarg;
	case TOK_PIPE:
		action->opcode = O_PIPE;
		goto chkarg;
	case TOK_SKIPTO:
		action->opcode = O_SKIPTO;
		goto chkarg;
	case TOK_NETGRAPH:
		action->opcode = O_NETGRAPH;
		goto chkarg;
	case TOK_NGTEE:
		action->opcode = O_NGTEE;
		goto chkarg;
	case TOK_DIVERT:
		action->opcode = O_DIVERT;
		goto chkarg;
	case TOK_TEE:
		action->opcode = O_TEE;
		goto chkarg;
	case TOK_CALL:
		action->opcode = O_CALLRETURN;
chkarg:
		if (!av[0])
			errx(EX_USAGE, "missing argument for %s", *(av - 1));
		if (isdigit(**av)) {
			action->arg1 = strtoul(*av, NULL, 10);
			if (action->arg1 <= 0 || action->arg1 >= IP_FW_TABLEARG)
				errx(EX_DATAERR, "illegal argument for %s",
				    *(av - 1));
		} else if (_substrcmp(*av, "tablearg") == 0) {
			action->arg1 = IP_FW_TARG;
		} else if (i == TOK_DIVERT || i == TOK_TEE) {
			struct servent *s;
			setservent(1);
			s = getservbyname(av[0], "divert");
			if (s != NULL)
				action->arg1 = ntohs(s->s_port);
			else
				errx(EX_DATAERR, "illegal divert/tee port");
		} else
			errx(EX_DATAERR, "illegal argument for %s", *(av - 1));
		av++;
		break;

	case TOK_FORWARD: {
		/*
		 * Locate the address-port separator (':' or ',').
		 * Could be one of the following:
		 *	hostname:port
		 *	IPv4 a.b.c.d,port
		 *	IPv4 a.b.c.d:port
		 *	IPv6 w:x:y::z,port
		 * The ':' can only be used with hostname and IPv4 address.
		 * XXX-BZ Should we also support [w:x:y::z]:port?
		 */
		struct sockaddr_storage result;
		struct addrinfo *res;
		char *s, *end;
		int family;
		u_short port_number;

		NEED1("missing forward address[:port]");

		/*
		 * locate the address-port separator (':' or ',')
		 */
		s = strchr(*av, ',');
		if (s == NULL) {
			/* Distinguish between IPv4:port and IPv6 cases. */
			s = strchr(*av, ':');
			if (s && strchr(s+1, ':'))
				s = NULL; /* no port */
		}

		port_number = 0;
		if (s != NULL) {
			/* Terminate host portion and set s to start of port. */
			*(s++) = '\0';
			i = strtoport(s, &end, 0 /* base */, 0 /* proto */);
			if (s == end)
				errx(EX_DATAERR,
				    "illegal forwarding port ``%s''", s);
			port_number = (u_short)i;
		}

		if (_substrcmp(*av, "tablearg") == 0) {
			family = PF_INET;
			((struct sockaddr_in*)&result)->sin_addr.s_addr =
			    INADDR_ANY;
		} else {
			/*
			 * Resolve the host name or address to a family and a
			 * network representation of the address.
			 */
			if (getaddrinfo(*av, NULL, NULL, &res))
				errx(EX_DATAERR, NULL);
			/* Just use the first host in the answer. */
			family = res->ai_family;
			memcpy(&result, res->ai_addr, res->ai_addrlen);
			freeaddrinfo(res);
		}

 		if (family == PF_INET) {
			ipfw_insn_sa *p = (ipfw_insn_sa *)action;

			action->opcode = O_FORWARD_IP;
			action->len = F_INSN_SIZE(ipfw_insn_sa);
			CHECK_ACTLEN;

			/*
			 * In the kernel we assume AF_INET and use only
			 * sin_port and sin_addr. Remember to set sin_len as
			 * the routing code seems to use it too.
			 */
			p->sa.sin_len = sizeof(struct sockaddr_in);
			p->sa.sin_family = AF_INET;
			p->sa.sin_port = port_number;
			p->sa.sin_addr.s_addr =
			     ((struct sockaddr_in *)&result)->sin_addr.s_addr;
		} else if (family == PF_INET6) {
			ipfw_insn_sa6 *p = (ipfw_insn_sa6 *)action;

			action->opcode = O_FORWARD_IP6;
			action->len = F_INSN_SIZE(ipfw_insn_sa6);
			CHECK_ACTLEN;

			p->sa.sin6_len = sizeof(struct sockaddr_in6);
			p->sa.sin6_family = AF_INET6;
			p->sa.sin6_port = port_number;
			p->sa.sin6_flowinfo = 0;
			p->sa.sin6_scope_id =
			    ((struct sockaddr_in6 *)&result)->sin6_scope_id;
			bcopy(&((struct sockaddr_in6*)&result)->sin6_addr,
			    &p->sa.sin6_addr, sizeof(p->sa.sin6_addr));
		} else {
			errx(EX_DATAERR, "Invalid address family in forward action");
		}
		av++;
		break;
	    }
	case TOK_COMMENT:
		/* pretend it is a 'count' rule followed by the comment */
		action->opcode = O_COUNT;
		av--;		/* go back... */
		break;

	case TOK_SETFIB:
	    {
		int numfibs;
		size_t intsize = sizeof(int);

		action->opcode = O_SETFIB;
		NEED1("missing fib number");
		if (_substrcmp(*av, "tablearg") == 0) {
			action->arg1 = IP_FW_TARG;
		} else {
		        action->arg1 = strtoul(*av, NULL, 10);
			if (sysctlbyname("net.fibs", &numfibs, &intsize,
			    NULL, 0) == -1)
				errx(EX_DATAERR, "fibs not suported.\n");
			if (action->arg1 >= numfibs)  /* Temporary */
				errx(EX_DATAERR, "fib too large.\n");
			/* Add high-order bit to fib to make room for tablearg*/
			action->arg1 |= 0x8000;
		}
		av++;
		break;
	    }

	case TOK_SETDSCP:
	    {
		int code;

		action->opcode = O_SETDSCP;
		NEED1("missing DSCP code");
		if (_substrcmp(*av, "tablearg") == 0) {
			action->arg1 = IP_FW_TARG;
		} else {
			if (isalpha(*av[0])) {
				if ((code = match_token(f_ipdscp, *av)) == -1)
					errx(EX_DATAERR, "Unknown DSCP code");
				action->arg1 = code;
			} else
			        action->arg1 = strtoul(*av, NULL, 10);
			/*
			 * Add high-order bit to DSCP to make room
			 * for tablearg
			 */
			action->arg1 |= 0x8000;
		}
		av++;
		break;
	    }

	case TOK_REASS:
		action->opcode = O_REASS;
		break;

	case TOK_RETURN:
		fill_cmd(action, O_CALLRETURN, F_NOT, 0);
		break;

	case TOK_TCPSETMSS: {
		u_long mss;
		uint16_t idx;

		idx = pack_object(tstate, "tcp-setmss", IPFW_TLV_EACTION);
		if (idx == 0)
			errx(EX_DATAERR, "pack_object failed");
		fill_cmd(action, O_EXTERNAL_ACTION, 0, idx);
		NEED1("Missing MSS value");
		action = next_cmd(action, &ablen);
		action->len = 1;
		CHECK_ACTLEN;
		mss = strtoul(*av, NULL, 10);
		if (mss == 0 || mss > UINT16_MAX)
			errx(EX_USAGE, "invalid MSS value %s", *av);
		fill_cmd(action, O_EXTERNAL_DATA, 0, (uint16_t)mss);
		av++;
		break;
	}

	default:
		av--;
		if (match_token(rule_eactions, *av) == -1)
			errx(EX_DATAERR, "invalid action %s\n", *av);
		/*
		 * External actions support.
		 * XXX: we support only syntax with instance name.
		 *	For known external actions (from rule_eactions list)
		 *	we can handle syntax directly. But with `eaction'
		 *	keyword we can use only `eaction <name> <instance>'
		 *	syntax.
		 */
	case TOK_EACTION: {
		uint16_t idx;

		NEED1("Missing eaction name");
		if (eaction_check_name(*av) != 0)
			errx(EX_DATAERR, "Invalid eaction name %s", *av);
		idx = pack_object(tstate, *av, IPFW_TLV_EACTION);
		if (idx == 0)
			errx(EX_DATAERR, "pack_object failed");
		fill_cmd(action, O_EXTERNAL_ACTION, 0, idx);
		av++;
		NEED1("Missing eaction instance name");
		action = next_cmd(action, &ablen);
		action->len = 1;
		CHECK_ACTLEN;
		if (eaction_check_name(*av) != 0)
			errx(EX_DATAERR, "Invalid eaction instance name %s",
			    *av);
		/*
		 * External action instance object has TLV type depended
		 * from the external action name object index. Since we
		 * currently don't know this index, use zero as TLV type.
		 */
		idx = pack_object(tstate, *av, 0);
		if (idx == 0)
			errx(EX_DATAERR, "pack_object failed");
		fill_cmd(action, O_EXTERNAL_INSTANCE, 0, idx);
		av++;
		}
	}
	action = next_cmd(action, &ablen);

	/*
	 * [altq queuename] -- altq tag, optional
	 * [log [logamount N]]	-- log, optional
	 *
	 * If they exist, it go first in the cmdbuf, but then it is
	 * skipped in the copy section to the end of the buffer.
	 */
	while (av[0] != NULL && (i = match_token(rule_action_params, *av)) != -1) {
		av++;
		switch (i) {
		case TOK_LOG:
		    {
			ipfw_insn_log *c = (ipfw_insn_log *)cmd;
			int l;

			if (have_log)
				errx(EX_DATAERR,
				    "log cannot be specified more than once");
			have_log = (ipfw_insn *)c;
			cmd->len = F_INSN_SIZE(ipfw_insn_log);
			CHECK_CMDLEN;
			cmd->opcode = O_LOG;
			if (av[0] && _substrcmp(*av, "logamount") == 0) {
				av++;
				NEED1("logamount requires argument");
				l = atoi(*av);
				if (l < 0)
					errx(EX_DATAERR,
					    "logamount must be positive");
				c->max_log = l;
				av++;
			} else {
				len = sizeof(c->max_log);
				if (sysctlbyname("net.inet.ip.fw.verbose_limit",
				    &c->max_log, &len, NULL, 0) == -1) {
					if (co.test_only) {
						c->max_log = 0;
						break;
					}
					errx(1, "sysctlbyname(\"%s\")",
					    "net.inet.ip.fw.verbose_limit");
				}
			}
		    }
			break;

#ifndef NO_ALTQ
		case TOK_ALTQ:
		    {
			ipfw_insn_altq *a = (ipfw_insn_altq *)cmd;

			NEED1("missing altq queue name");
			if (have_altq)
				errx(EX_DATAERR,
				    "altq cannot be specified more than once");
			have_altq = (ipfw_insn *)a;
			cmd->len = F_INSN_SIZE(ipfw_insn_altq);
			CHECK_CMDLEN;
			cmd->opcode = O_ALTQ;
			a->qid = altq_name_to_qid(*av);
			av++;
		    }
			break;
#endif

		case TOK_TAG:
		case TOK_UNTAG: {
			uint16_t tag;

			if (have_tag)
				errx(EX_USAGE, "tag and untag cannot be "
				    "specified more than once");
			GET_UINT_ARG(tag, IPFW_ARG_MIN, IPFW_ARG_MAX, i,
			   rule_action_params);
			have_tag = cmd;
			fill_cmd(cmd, O_TAG, (i == TOK_TAG) ? 0: F_NOT, tag);
			av++;
			break;
		}

		default:
			abort();
		}
		cmd = next_cmd(cmd, &cblen);
	}

	if (have_state)	{ /* must be a check-state, we are done */
		if (*av != NULL &&
		    match_token(rule_options, *av) == TOK_COMMENT) {
			/* check-state has a comment */
			av++;
			fill_comment(cmd, av, cblen);
			cmd = next_cmd(cmd, &cblen);
			av[0] = NULL;
		}
		goto done;
	}

#define OR_START(target)					\
	if (av[0] && (*av[0] == '(' || *av[0] == '{')) { 	\
		if (open_par)					\
			errx(EX_USAGE, "nested \"(\" not allowed\n"); \
		prev = NULL;					\
		open_par = 1;					\
		if ( (av[0])[1] == '\0') {			\
			av++;					\
		} else						\
			(*av)++;				\
	}							\
	target:							\


#define	CLOSE_PAR						\
	if (open_par) {						\
		if (av[0] && (					\
		    strcmp(*av, ")") == 0 ||			\
		    strcmp(*av, "}") == 0)) {			\
			prev = NULL;				\
			open_par = 0;				\
			av++;					\
		} else						\
			errx(EX_USAGE, "missing \")\"\n");	\
	}

#define NOT_BLOCK						\
	if (av[0] && _substrcmp(*av, "not") == 0) {		\
		if (cmd->len & F_NOT)				\
			errx(EX_USAGE, "double \"not\" not allowed\n"); \
		cmd->len |= F_NOT;				\
		av++;						\
	}

#define OR_BLOCK(target)					\
	if (av[0] && _substrcmp(*av, "or") == 0) {		\
		if (prev == NULL || open_par == 0)		\
			errx(EX_DATAERR, "invalid OR block");	\
		prev->len |= F_OR;				\
		av++;					\
		goto target;					\
	}							\
	CLOSE_PAR;

	first_cmd = cmd;

#if 0
	/*
	 * MAC addresses, optional.
	 * If we have this, we skip the part "proto from src to dst"
	 * and jump straight to the option parsing.
	 */
	NOT_BLOCK;
	NEED1("missing protocol");
	if (_substrcmp(*av, "MAC") == 0 ||
	    _substrcmp(*av, "mac") == 0) {
		av++;			/* the "MAC" keyword */
		add_mac(cmd, av);	/* exits in case of errors */
		cmd = next_cmd(cmd);
		av += 2;		/* dst-mac and src-mac */
		NOT_BLOCK;
		NEED1("missing mac type");
		if (add_mactype(cmd, av[0]))
			cmd = next_cmd(cmd);
		av++;			/* any or mac-type */
		goto read_options;
	}
#endif

	/*
	 * protocol, mandatory
	 */
    OR_START(get_proto);
	NOT_BLOCK;
	NEED1("missing protocol");
	if (add_proto_compat(cmd, *av, &proto)) {
		av++;
		if (F_LEN(cmd) != 0) {
			prev = cmd;
			cmd = next_cmd(cmd, &cblen);
		}
	} else if (first_cmd != cmd) {
		errx(EX_DATAERR, "invalid protocol ``%s''", *av);
	} else {
		rule->flags |= IPFW_RULE_JUSTOPTS;
		goto read_options;
	}
    OR_BLOCK(get_proto);

	/*
	 * "from", mandatory
	 */
	if ((av[0] == NULL) || _substrcmp(*av, "from") != 0)
		errx(EX_USAGE, "missing ``from''");
	av++;

	/*
	 * source IP, mandatory
	 */
    OR_START(source_ip);
	NOT_BLOCK;	/* optional "not" */
	NEED1("missing source address");
	if (add_src(cmd, *av, proto, cblen, tstate)) {
		av++;
		if (F_LEN(cmd) != 0) {	/* ! any */
			prev = cmd;
			cmd = next_cmd(cmd, &cblen);
		}
	} else
		errx(EX_USAGE, "bad source address %s", *av);
    OR_BLOCK(source_ip);

	/*
	 * source ports, optional
	 */
	NOT_BLOCK;	/* optional "not" */
	if ( av[0] != NULL ) {
		if (_substrcmp(*av, "any") == 0 ||
		    add_ports(cmd, *av, proto, O_IP_SRCPORT, cblen)) {
			av++;
			if (F_LEN(cmd) != 0)
				cmd = next_cmd(cmd, &cblen);
		}
	}

	/*
	 * "to", mandatory
	 */
	if ( (av[0] == NULL) || _substrcmp(*av, "to") != 0 )
		errx(EX_USAGE, "missing ``to''");
	av++;

	/*
	 * destination, mandatory
	 */
    OR_START(dest_ip);
	NOT_BLOCK;	/* optional "not" */
	NEED1("missing dst address");
	if (add_dst(cmd, *av, proto, cblen, tstate)) {
		av++;
		if (F_LEN(cmd) != 0) {	/* ! any */
			prev = cmd;
			cmd = next_cmd(cmd, &cblen);
		}
	} else
		errx( EX_USAGE, "bad destination address %s", *av);
    OR_BLOCK(dest_ip);

	/*
	 * dest. ports, optional
	 */
	NOT_BLOCK;	/* optional "not" */
	if (av[0]) {
		if (_substrcmp(*av, "any") == 0 ||
		    add_ports(cmd, *av, proto, O_IP_DSTPORT, cblen)) {
			av++;
			if (F_LEN(cmd) != 0)
				cmd = next_cmd(cmd, &cblen);
		}
	}

read_options:
	prev = NULL;
	while ( av[0] != NULL ) {
		char *s;
		ipfw_insn_u32 *cmd32;	/* alias for cmd */

		s = *av;
		cmd32 = (ipfw_insn_u32 *)cmd;

		if (*s == '!') {	/* alternate syntax for NOT */
			if (cmd->len & F_NOT)
				errx(EX_USAGE, "double \"not\" not allowed\n");
			cmd->len = F_NOT;
			s++;
		}
		i = match_token(rule_options, s);
		av++;
		switch(i) {
		case TOK_NOT:
			if (cmd->len & F_NOT)
				errx(EX_USAGE, "double \"not\" not allowed\n");
			cmd->len = F_NOT;
			break;

		case TOK_OR:
			if (open_par == 0 || prev == NULL)
				errx(EX_USAGE, "invalid \"or\" block\n");
			prev->len |= F_OR;
			break;

		case TOK_STARTBRACE:
			if (open_par)
				errx(EX_USAGE, "+nested \"(\" not allowed\n");
			open_par = 1;
			break;

		case TOK_ENDBRACE:
			if (!open_par)
				errx(EX_USAGE, "+missing \")\"\n");
			open_par = 0;
			prev = NULL;
			break;

		case TOK_IN:
			fill_cmd(cmd, O_IN, 0, 0);
			break;

		case TOK_OUT:
			cmd->len ^= F_NOT; /* toggle F_NOT */
			fill_cmd(cmd, O_IN, 0, 0);
			break;

		case TOK_DIVERTED:
			fill_cmd(cmd, O_DIVERTED, 0, 3);
			break;

		case TOK_DIVERTEDLOOPBACK:
			fill_cmd(cmd, O_DIVERTED, 0, 1);
			break;

		case TOK_DIVERTEDOUTPUT:
			fill_cmd(cmd, O_DIVERTED, 0, 2);
			break;

		case TOK_FRAG:
			fill_cmd(cmd, O_FRAG, 0, 0);
			break;

		case TOK_LAYER2:
			fill_cmd(cmd, O_LAYER2, 0, 0);
			break;

		case TOK_XMIT:
		case TOK_RECV:
		case TOK_VIA:
			NEED1("recv, xmit, via require interface name"
				" or address");
			fill_iface((ipfw_insn_if *)cmd, av[0], cblen, tstate);
			av++;
			if (F_LEN(cmd) == 0)	/* not a valid address */
				break;
			if (i == TOK_XMIT)
				cmd->opcode = O_XMIT;
			else if (i == TOK_RECV)
				cmd->opcode = O_RECV;
			else if (i == TOK_VIA)
				cmd->opcode = O_VIA;
			break;

		case TOK_ICMPTYPES:
			NEED1("icmptypes requires list of types");
			fill_icmptypes((ipfw_insn_u32 *)cmd, *av);
			av++;
			break;

		case TOK_ICMP6TYPES:
			NEED1("icmptypes requires list of types");
			fill_icmp6types((ipfw_insn_icmp6 *)cmd, *av, cblen);
			av++;
			break;

		case TOK_IPTTL:
			NEED1("ipttl requires TTL");
			if (strpbrk(*av, "-,")) {
			    if (!add_ports(cmd, *av, 0, O_IPTTL, cblen))
				errx(EX_DATAERR, "invalid ipttl %s", *av);
			} else
			    fill_cmd(cmd, O_IPTTL, 0, strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_IPID:
			NEED1("ipid requires id");
			if (strpbrk(*av, "-,")) {
			    if (!add_ports(cmd, *av, 0, O_IPID, cblen))
				errx(EX_DATAERR, "invalid ipid %s", *av);
			} else
			    fill_cmd(cmd, O_IPID, 0, strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_IPLEN:
			NEED1("iplen requires length");
			if (strpbrk(*av, "-,")) {
			    if (!add_ports(cmd, *av, 0, O_IPLEN, cblen))
				errx(EX_DATAERR, "invalid ip len %s", *av);
			} else
			    fill_cmd(cmd, O_IPLEN, 0, strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_IPVER:
			NEED1("ipver requires version");
			fill_cmd(cmd, O_IPVER, 0, strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_IPPRECEDENCE:
			NEED1("ipprecedence requires value");
			fill_cmd(cmd, O_IPPRECEDENCE, 0,
			    (strtoul(*av, NULL, 0) & 7) << 5);
			av++;
			break;

		case TOK_DSCP:
			NEED1("missing DSCP code");
			fill_dscp(cmd, *av, cblen);
			av++;
			break;

		case TOK_IPOPTS:
			NEED1("missing argument for ipoptions");
			fill_flags_cmd(cmd, O_IPOPT, f_ipopts, *av);
			av++;
			break;

		case TOK_IPTOS:
			NEED1("missing argument for iptos");
			fill_flags_cmd(cmd, O_IPTOS, f_iptos, *av);
			av++;
			break;

		case TOK_UID:
			NEED1("uid requires argument");
		    {
			char *end;
			uid_t uid;
			struct passwd *pwd;

			cmd->opcode = O_UID;
			uid = strtoul(*av, &end, 0);
			pwd = (*end == '\0') ? getpwuid(uid) : getpwnam(*av);
			if (pwd == NULL)
				errx(EX_DATAERR, "uid \"%s\" nonexistent", *av);
			cmd32->d[0] = pwd->pw_uid;
			cmd->len |= F_INSN_SIZE(ipfw_insn_u32);
			av++;
		    }
			break;

		case TOK_GID:
			NEED1("gid requires argument");
		    {
			char *end;
			gid_t gid;
			struct group *grp;

			cmd->opcode = O_GID;
			gid = strtoul(*av, &end, 0);
			grp = (*end == '\0') ? getgrgid(gid) : getgrnam(*av);
			if (grp == NULL)
				errx(EX_DATAERR, "gid \"%s\" nonexistent", *av);
			cmd32->d[0] = grp->gr_gid;
			cmd->len |= F_INSN_SIZE(ipfw_insn_u32);
			av++;
		    }
			break;

		case TOK_JAIL:
			NEED1("jail requires argument");
		    {
			int jid;

			cmd->opcode = O_JAIL;
			jid = jail_getid(*av);
			if (jid < 0)
				errx(EX_DATAERR, "%s", jail_errmsg);
			cmd32->d[0] = (uint32_t)jid;
			cmd->len |= F_INSN_SIZE(ipfw_insn_u32);
			av++;
		    }
			break;

		case TOK_ESTAB:
			fill_cmd(cmd, O_ESTAB, 0, 0);
			break;

		case TOK_SETUP:
			fill_cmd(cmd, O_TCPFLAGS, 0,
				(TH_SYN) | ( (TH_ACK) & 0xff) <<8 );
			break;

		case TOK_TCPDATALEN:
			NEED1("tcpdatalen requires length");
			if (strpbrk(*av, "-,")) {
			    if (!add_ports(cmd, *av, 0, O_TCPDATALEN, cblen))
				errx(EX_DATAERR, "invalid tcpdata len %s", *av);
			} else
			    fill_cmd(cmd, O_TCPDATALEN, 0,
				    strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_TCPOPTS:
			NEED1("missing argument for tcpoptions");
			fill_flags_cmd(cmd, O_TCPOPTS, f_tcpopts, *av);
			av++;
			break;

		case TOK_TCPSEQ:
		case TOK_TCPACK:
			NEED1("tcpseq/tcpack requires argument");
			cmd->len = F_INSN_SIZE(ipfw_insn_u32);
			cmd->opcode = (i == TOK_TCPSEQ) ? O_TCPSEQ : O_TCPACK;
			cmd32->d[0] = htonl(strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_TCPWIN:
			NEED1("tcpwin requires length");
			if (strpbrk(*av, "-,")) {
			    if (!add_ports(cmd, *av, 0, O_TCPWIN, cblen))
				errx(EX_DATAERR, "invalid tcpwin len %s", *av);
			} else
			    fill_cmd(cmd, O_TCPWIN, 0,
				    strtoul(*av, NULL, 0));
			av++;
			break;

		case TOK_TCPFLAGS:
			NEED1("missing argument for tcpflags");
			cmd->opcode = O_TCPFLAGS;
			fill_flags_cmd(cmd, O_TCPFLAGS, f_tcpflags, *av);
			av++;
			break;

		case TOK_KEEPSTATE:
		case TOK_RECORDSTATE: {
			uint16_t uidx;

			if (open_par)
				errx(EX_USAGE, "keep-state or record-state cannot be part "
				    "of an or block");
			if (have_state)
				errx(EX_USAGE, "only one of keep-state, record-state, "
					" limit and set-limit is allowed");
			if (*av != NULL && *av[0] == ':') {
				if (state_check_name(*av + 1) != 0)
					errx(EX_DATAERR,
					    "Invalid state name %s", *av);
				uidx = pack_object(tstate, *av + 1,
				    IPFW_TLV_STATE_NAME);
				av++;
			} else
				uidx = pack_object(tstate, default_state_name,
				    IPFW_TLV_STATE_NAME);
			have_state = cmd;
			have_rstate = i == TOK_RECORDSTATE;
			fill_cmd(cmd, O_KEEP_STATE, 0, uidx);
			break;
		}

		case TOK_LIMIT:
		case TOK_SETLIMIT: {
			ipfw_insn_limit *c = (ipfw_insn_limit *)cmd;
			int val;

			if (open_par)
				errx(EX_USAGE,
				    "limit or set-limit cannot be part of an or block");
			if (have_state)
				errx(EX_USAGE, "only one of keep-state, record-state, "
					" limit and set-limit is allowed");
			have_state = cmd;
			have_rstate = i == TOK_SETLIMIT;

			cmd->len = F_INSN_SIZE(ipfw_insn_limit);
			CHECK_CMDLEN;
			cmd->opcode = O_LIMIT;
			c->limit_mask = c->conn_limit = 0;

			while ( av[0] != NULL ) {
				if ((val = match_token(limit_masks, *av)) <= 0)
					break;
				c->limit_mask |= val;
				av++;
			}

			if (c->limit_mask == 0)
				errx(EX_USAGE, "limit: missing limit mask");

			GET_UINT_ARG(c->conn_limit, IPFW_ARG_MIN, IPFW_ARG_MAX,
			    TOK_LIMIT, rule_options);
			av++;

			if (*av != NULL && *av[0] == ':') {
				if (state_check_name(*av + 1) != 0)
					errx(EX_DATAERR,
					    "Invalid state name %s", *av);
				cmd->arg1 = pack_object(tstate, *av + 1,
				    IPFW_TLV_STATE_NAME);
				av++;
			} else
				cmd->arg1 = pack_object(tstate,
				    default_state_name, IPFW_TLV_STATE_NAME);
			break;
		}

		case TOK_PROTO:
			NEED1("missing protocol");
			if (add_proto(cmd, *av, &proto)) {
				av++;
			} else
				errx(EX_DATAERR, "invalid protocol ``%s''",
				    *av);
			break;

		case TOK_SRCIP:
			NEED1("missing source IP");
			if (add_srcip(cmd, *av, cblen, tstate)) {
				av++;
			}
			break;

		case TOK_DSTIP:
			NEED1("missing destination IP");
			if (add_dstip(cmd, *av, cblen, tstate)) {
				av++;
			}
			break;

		case TOK_SRCIP6:
			NEED1("missing source IP6");
			if (add_srcip6(cmd, *av, cblen, tstate)) {
				av++;
			}
			break;

		case TOK_DSTIP6:
			NEED1("missing destination IP6");
			if (add_dstip6(cmd, *av, cblen, tstate)) {
				av++;
			}
			break;

		case TOK_SRCPORT:
			NEED1("missing source port");
			if (_substrcmp(*av, "any") == 0 ||
			    add_ports(cmd, *av, proto, O_IP_SRCPORT, cblen)) {
				av++;
			} else
				errx(EX_DATAERR, "invalid source port %s", *av);
			break;

		case TOK_DSTPORT:
			NEED1("missing destination port");
			if (_substrcmp(*av, "any") == 0 ||
			    add_ports(cmd, *av, proto, O_IP_DSTPORT, cblen)) {
				av++;
			} else
				errx(EX_DATAERR, "invalid destination port %s",
				    *av);
			break;

		case TOK_MAC:
			if (add_mac(cmd, av, cblen))
				av += 2;
			break;

		case TOK_MACTYPE:
			NEED1("missing mac type");
			if (!add_mactype(cmd, *av, cblen))
				errx(EX_DATAERR, "invalid mac type %s", *av);
			av++;
			break;

		case TOK_VERREVPATH:
			fill_cmd(cmd, O_VERREVPATH, 0, 0);
			break;

		case TOK_VERSRCREACH:
			fill_cmd(cmd, O_VERSRCREACH, 0, 0);
			break;

		case TOK_ANTISPOOF:
			fill_cmd(cmd, O_ANTISPOOF, 0, 0);
			break;

		case TOK_IPSEC:
			fill_cmd(cmd, O_IPSEC, 0, 0);
			break;

		case TOK_IPV6:
			fill_cmd(cmd, O_IP6, 0, 0);
			break;

		case TOK_IPV4:
			fill_cmd(cmd, O_IP4, 0, 0);
			break;

		case TOK_EXT6HDR:
			fill_ext6hdr( cmd, *av );
			av++;
			break;

		case TOK_FLOWID:
			if (proto != IPPROTO_IPV6 )
				errx( EX_USAGE, "flow-id filter is active "
				    "only for ipv6 protocol\n");
			fill_flow6( (ipfw_insn_u32 *) cmd, *av, cblen);
			av++;
			break;

		case TOK_COMMENT:
			fill_comment(cmd, av, cblen);
			av[0]=NULL;
			break;

		case TOK_TAGGED:
			if (av[0] && strpbrk(*av, "-,")) {
				if (!add_ports(cmd, *av, 0, O_TAGGED, cblen))
					errx(EX_DATAERR, "tagged: invalid tag"
					    " list: %s", *av);
			}
			else {
				uint16_t tag;

				GET_UINT_ARG(tag, IPFW_ARG_MIN, IPFW_ARG_MAX,
				    TOK_TAGGED, rule_options);
				fill_cmd(cmd, O_TAGGED, 0, tag);
			}
			av++;
			break;

		case TOK_FIB:
			NEED1("fib requires fib number");
			fill_cmd(cmd, O_FIB, 0, strtoul(*av, NULL, 0));
			av++;
			break;
		case TOK_SOCKARG:
			fill_cmd(cmd, O_SOCKARG, 0, 0);
			break;

		case TOK_LOOKUP: {
			ipfw_insn_u32 *c = (ipfw_insn_u32 *)cmd;
			int j;

			if (!av[0] || !av[1])
				errx(EX_USAGE, "format: lookup argument tablenum");
			cmd->opcode = O_IP_DST_LOOKUP;
			cmd->len |= F_INSN_SIZE(ipfw_insn) + 2;
			i = match_token(rule_options, *av);
			for (j = 0; lookup_key[j] >= 0 ; j++) {
				if (i == lookup_key[j])
					break;
			}
			if (lookup_key[j] <= 0)
				errx(EX_USAGE, "format: cannot lookup on %s", *av);
			__PAST_END(c->d, 1) = j; // i converted to option
			av++;

			if ((j = pack_table(tstate, *av)) == 0)
				errx(EX_DATAERR, "Invalid table name: %s", *av);

			cmd->arg1 = j;
			av++;
		    }
			break;
		case TOK_FLOW:
			NEED1("missing table name");
			if (strncmp(*av, "table(", 6) != 0)
				errx(EX_DATAERR,
				    "enclose table name into \"table()\"");
			fill_table(cmd, *av, O_IP_FLOW_LOOKUP, tstate);
			av++;
			break;

		case TOK_SKIPACTION:
			if (have_skipcmd)
				errx(EX_USAGE, "only one defer-action "
					"is allowed");
			have_skipcmd = cmd;
			fill_cmd(cmd, O_SKIP_ACTION, 0, 0);
			break;

		default:
			errx(EX_USAGE, "unrecognised option [%d] %s\n", i, s);
		}
		if (F_LEN(cmd) > 0) {	/* prepare to advance */
			prev = cmd;
			cmd = next_cmd(cmd, &cblen);
		}
	}

done:

	if (!have_state && have_skipcmd)
		warnx("Rule contains \"defer-immediate-action\" "
			"and doesn't contain any state-related options.");

	/*
	 * Now copy stuff into the rule.
	 * If we have a keep-state option, the first instruction
	 * must be a PROBE_STATE (which is generated here).
	 * If we have a LOG option, it was stored as the first command,
	 * and now must be moved to the top of the action part.
	 */
	dst = (ipfw_insn *)rule->cmd;

	/*
	 * First thing to write into the command stream is the match probability.
	 */
	if (match_prob != 1) { /* 1 means always match */
		dst->opcode = O_PROB;
		dst->len = 2;
		*((int32_t *)(dst+1)) = (int32_t)(match_prob * 0x7fffffff);
		dst += dst->len;
	}

	/*
	 * generate O_PROBE_STATE if necessary
	 */
	if (have_state && have_state->opcode != O_CHECK_STATE && !have_rstate) {
		fill_cmd(dst, O_PROBE_STATE, 0, have_state->arg1);
		dst = next_cmd(dst, &rblen);
	}

	/*
	 * copy all commands but O_LOG, O_KEEP_STATE, O_LIMIT, O_ALTQ, O_TAG,
	 * O_SKIP_ACTION
	 */
	for (src = (ipfw_insn *)cmdbuf; src != cmd; src += i) {
		i = F_LEN(src);
		CHECK_RBUFLEN(i);

		switch (src->opcode) {
		case O_LOG:
		case O_KEEP_STATE:
		case O_LIMIT:
		case O_ALTQ:
		case O_TAG:
		case O_SKIP_ACTION:
			break;
		default:
			bcopy(src, dst, i * sizeof(uint32_t));
			dst += i;
		}
	}

	/*
	 * put back the have_state command as last opcode
	 */
	if (have_state && have_state->opcode != O_CHECK_STATE) {
		i = F_LEN(have_state);
		CHECK_RBUFLEN(i);
		bcopy(have_state, dst, i * sizeof(uint32_t));
		dst += i;
	}

	/*
	 * put back the have_skipcmd command as very last opcode
	 */
	if (have_skipcmd) {
		i = F_LEN(have_skipcmd);
		CHECK_RBUFLEN(i);
		bcopy(have_skipcmd, dst, i * sizeof(uint32_t));
		dst += i;
	}

	/*
	 * start action section
	 */
	rule->act_ofs = dst - rule->cmd;

	/* put back O_LOG, O_ALTQ, O_TAG if necessary */
	if (have_log) {
		i = F_LEN(have_log);
		CHECK_RBUFLEN(i);
		bcopy(have_log, dst, i * sizeof(uint32_t));
		dst += i;
	}
	if (have_altq) {
		i = F_LEN(have_altq);
		CHECK_RBUFLEN(i);
		bcopy(have_altq, dst, i * sizeof(uint32_t));
		dst += i;
	}
	if (have_tag) {
		i = F_LEN(have_tag);
		CHECK_RBUFLEN(i);
		bcopy(have_tag, dst, i * sizeof(uint32_t));
		dst += i;
	}

	/*
	 * copy all other actions
	 */
	for (src = (ipfw_insn *)actbuf; src != action; src += i) {
		i = F_LEN(src);
		CHECK_RBUFLEN(i);
		bcopy(src, dst, i * sizeof(uint32_t));
		dst += i;
	}

	rule->cmd_len = (uint32_t *)dst - (uint32_t *)(rule->cmd);
	*rbufsize = (char *)dst - (char *)rule;
}

static int
compare_ntlv(const void *_a, const void *_b)
{
	ipfw_obj_ntlv *a, *b;

	a = (ipfw_obj_ntlv *)_a;
	b = (ipfw_obj_ntlv *)_b;

	if (a->set < b->set)
		return (-1);
	else if (a->set > b->set)
		return (1);

	if (a->idx < b->idx)
		return (-1);
	else if (a->idx > b->idx)
		return (1);

	if (a->head.type < b->head.type)
		return (-1);
	else if (a->head.type > b->head.type)
		return (1);

	return (0);
}

/*
 * Provide kernel with sorted list of referenced objects
 */
static void
object_sort_ctlv(ipfw_obj_ctlv *ctlv)
{

	qsort(ctlv + 1, ctlv->count, ctlv->objsize, compare_ntlv);
}

struct object_kt {
	uint16_t	uidx;
	uint16_t	type;
};
static int
compare_object_kntlv(const void *k, const void *v)
{
	ipfw_obj_ntlv *ntlv;
	struct object_kt key;

	key = *((struct object_kt *)k);
	ntlv = (ipfw_obj_ntlv *)v;

	if (key.uidx < ntlv->idx)
		return (-1);
	else if (key.uidx > ntlv->idx)
		return (1);

	if (key.type < ntlv->head.type)
		return (-1);
	else if (key.type > ntlv->head.type)
		return (1);

	return (0);
}

/*
 * Finds object name in @ctlv by @idx and @type.
 * Uses the following facts:
 * 1) All TLVs are the same size
 * 2) Kernel implementation provides already sorted list.
 *
 * Returns table name or NULL.
 */
static char *
object_search_ctlv(ipfw_obj_ctlv *ctlv, uint16_t idx, uint16_t type)
{
	ipfw_obj_ntlv *ntlv;
	struct object_kt key;

	key.uidx = idx;
	key.type = type;

	ntlv = bsearch(&key, (ctlv + 1), ctlv->count, ctlv->objsize,
	    compare_object_kntlv);

	if (ntlv != NULL)
		return (ntlv->name);

	return (NULL);
}

static char *
table_search_ctlv(ipfw_obj_ctlv *ctlv, uint16_t idx)
{

	return (object_search_ctlv(ctlv, idx, IPFW_TLV_TBL_NAME));
}

/*
 * Adds one or more rules to ipfw chain.
 * Data layout:
 * Request:
 * [
 *   ip_fw3_opheader
 *   [ ipfw_obj_ctlv(IPFW_TLV_TBL_LIST) ipfw_obj_ntlv x N ] (optional *1)
 *   [ ipfw_obj_ctlv(IPFW_TLV_RULE_LIST) [ ip_fw_rule ip_fw_insn ] x N ] (*2) (*3)
 * ]
 * Reply:
 * [
 *   ip_fw3_opheader
 *   [ ipfw_obj_ctlv(IPFW_TLV_TBL_LIST) ipfw_obj_ntlv x N ] (optional)
 *   [ ipfw_obj_ctlv(IPFW_TLV_RULE_LIST) [ ip_fw_rule ip_fw_insn ] x N ]
 * ]
 *
 * Rules in reply are modified to store their actual ruleset number.
 *
 * (*1) TLVs inside IPFW_TLV_TBL_LIST needs to be sorted ascending
 * according to their idx field and there has to be no duplicates.
 * (*2) Numbered rules inside IPFW_TLV_RULE_LIST needs to be sorted ascending.
 * (*3) Each ip_fw structure needs to be aligned to u64 boundary.
 */
void
ipfw_add(char *av[])
{
	uint32_t rulebuf[1024];
	int rbufsize, default_off, tlen, rlen;
	size_t sz;
	struct tidx ts;
	struct ip_fw_rule *rule;
	caddr_t tbuf;
	ip_fw3_opheader *op3;
	ipfw_obj_ctlv *ctlv, *tstate;

	rbufsize = sizeof(rulebuf);
	memset(rulebuf, 0, rbufsize);
	memset(&ts, 0, sizeof(ts));

	/* Optimize case with no tables */
	default_off = sizeof(ipfw_obj_ctlv) + sizeof(ip_fw3_opheader);
	op3 = (ip_fw3_opheader *)rulebuf;
	ctlv = (ipfw_obj_ctlv *)(op3 + 1);
	rule = (struct ip_fw_rule *)(ctlv + 1);
	rbufsize -= default_off;

	compile_rule(av, (uint32_t *)rule, &rbufsize, &ts);
	/* Align rule size to u64 boundary */
	rlen = roundup2(rbufsize, sizeof(uint64_t));

	tbuf = NULL;
	sz = 0;
	tstate = NULL;
	if (ts.count != 0) {
		/* Some tables. We have to alloc more data */
		tlen = ts.count * sizeof(ipfw_obj_ntlv);
		sz = default_off + sizeof(ipfw_obj_ctlv) + tlen + rlen;

		if ((tbuf = calloc(1, sz)) == NULL)
			err(EX_UNAVAILABLE, "malloc() failed for IP_FW_ADD");
		op3 = (ip_fw3_opheader *)tbuf;
		/* Tables first */
		ctlv = (ipfw_obj_ctlv *)(op3 + 1);
		ctlv->head.type = IPFW_TLV_TBLNAME_LIST;
		ctlv->head.length = sizeof(ipfw_obj_ctlv) + tlen;
		ctlv->count = ts.count;
		ctlv->objsize = sizeof(ipfw_obj_ntlv);
		memcpy(ctlv + 1, ts.idx, tlen);
		object_sort_ctlv(ctlv);
		tstate = ctlv;
		/* Rule next */
		ctlv = (ipfw_obj_ctlv *)((caddr_t)ctlv + ctlv->head.length);
		ctlv->head.type = IPFW_TLV_RULE_LIST;
		ctlv->head.length = sizeof(ipfw_obj_ctlv) + rlen;
		ctlv->count = 1;
		memcpy(ctlv + 1, rule, rbufsize);
	} else {
		/* Simply add header */
		sz = rlen + default_off;
		memset(ctlv, 0, sizeof(*ctlv));
		ctlv->head.type = IPFW_TLV_RULE_LIST;
		ctlv->head.length = sizeof(ipfw_obj_ctlv) + rlen;
		ctlv->count = 1;
	}

	if (do_get3(IP_FW_XADD, op3, &sz) != 0)
		err(EX_UNAVAILABLE, "getsockopt(%s)", "IP_FW_XADD");

	if (!co.do_quiet) {
		struct format_opts sfo;
		struct buf_pr bp;
		memset(&sfo, 0, sizeof(sfo));
		sfo.tstate = tstate;
		sfo.set_mask = (uint32_t)(-1);
		bp_alloc(&bp, 4096);
		show_static_rule(&co, &sfo, &bp, rule, NULL);
		printf("%s", bp.buf);
		bp_free(&bp);
	}

	if (tbuf != NULL)
		free(tbuf);

	if (ts.idx != NULL)
		free(ts.idx);
}

/*
 * clear the counters or the log counters.
 * optname has the following values:
 *  0 (zero both counters and logging)
 *  1 (zero logging only)
 */
void
ipfw_zero(int ac, char *av[], int optname)
{
	ipfw_range_tlv rt;
	char const *errstr;
	char const *name = optname ? "RESETLOG" : "ZERO";
	uint32_t arg;
	int failed = EX_OK;

	optname = optname ? IP_FW_XRESETLOG : IP_FW_XZERO;
	av++; ac--;

	if (ac == 0) {
		/* clear all entries */
		memset(&rt, 0, sizeof(rt));
		rt.flags = IPFW_RCFLAG_ALL;
		if (do_range_cmd(optname, &rt) < 0)
			err(EX_UNAVAILABLE, "setsockopt(IP_FW_X%s)", name);
		if (!co.do_quiet)
			printf("%s.\n", optname == IP_FW_XZERO ?
			    "Accounting cleared":"Logging counts reset");

		return;
	}

	while (ac) {
		/* Rule number */
		if (isdigit(**av)) {
			arg = strtonum(*av, 0, 0xffff, &errstr);
			if (errstr)
				errx(EX_DATAERR,
				    "invalid rule number %s\n", *av);
			memset(&rt, 0, sizeof(rt));
			rt.start_rule = arg;
			rt.end_rule = arg;
			rt.flags |= IPFW_RCFLAG_RANGE;
			if (co.use_set != 0) {
				rt.set = co.use_set - 1;
				rt.flags |= IPFW_RCFLAG_SET;
			}
			if (do_range_cmd(optname, &rt) != 0) {
				warn("rule %u: setsockopt(IP_FW_X%s)",
				    arg, name);
				failed = EX_UNAVAILABLE;
			} else if (rt.new_set == 0) {
				printf("Entry %d not found\n", arg);
				failed = EX_UNAVAILABLE;
			} else if (!co.do_quiet)
				printf("Entry %d %s.\n", arg,
				    optname == IP_FW_XZERO ?
					"cleared" : "logging count reset");
		} else {
			errx(EX_USAGE, "invalid rule number ``%s''", *av);
		}
		av++; ac--;
	}
	if (failed != EX_OK)
		exit(failed);
}

void
ipfw_flush(int force)
{
	ipfw_range_tlv rt;

	if (!force && !co.do_quiet) { /* need to ask user */
		int c;

		printf("Are you sure? [yn] ");
		fflush(stdout);
		do {
			c = toupper(getc(stdin));
			while (c != '\n' && getc(stdin) != '\n')
				if (feof(stdin))
					return; /* and do not flush */
		} while (c != 'Y' && c != 'N');
		printf("\n");
		if (c == 'N')	/* user said no */
			return;
	}
	if (co.do_pipe) {
		dummynet_flush();
		return;
	}
	/* `ipfw set N flush` - is the same that `ipfw delete set N` */
	memset(&rt, 0, sizeof(rt));
	if (co.use_set != 0) {
		rt.set = co.use_set - 1;
		rt.flags = IPFW_RCFLAG_SET;
	} else
		rt.flags = IPFW_RCFLAG_ALL;
	if (do_range_cmd(IP_FW_XDEL, &rt) != 0)
			err(EX_UNAVAILABLE, "setsockopt(IP_FW_XDEL)");
	if (!co.do_quiet)
		printf("Flushed all %s.\n", co.do_pipe ? "pipes" : "rules");
}

static struct _s_x intcmds[] = {
      { "talist",	TOK_TALIST },
      { "iflist",	TOK_IFLIST },
      { "olist",	TOK_OLIST },
      { "vlist",	TOK_VLIST },
      { NULL, 0 }
};

static struct _s_x otypes[] = {
	{ "EACTION",	IPFW_TLV_EACTION },
	{ "DYNSTATE",	IPFW_TLV_STATE_NAME },
	{ NULL, 0 }
};

static const char*
lookup_eaction_name(ipfw_obj_ntlv *ntlv, int cnt, uint16_t type)
{
	const char *name;
	int i;

	name = NULL;
	for (i = 0; i < cnt; i++) {
		if (ntlv[i].head.type != IPFW_TLV_EACTION)
			continue;
		if (IPFW_TLV_EACTION_NAME(ntlv[i].idx) != type)
			continue;
		name = ntlv[i].name;
		break;
	}
	return (name);
}

static void
ipfw_list_objects(int ac, char *av[])
{
	ipfw_obj_lheader req, *olh;
	ipfw_obj_ntlv *ntlv;
	const char *name;
	size_t sz;
	int i;

	memset(&req, 0, sizeof(req));
	sz = sizeof(req);
	if (do_get3(IP_FW_DUMP_SRVOBJECTS, &req.opheader, &sz) != 0)
		if (errno != ENOMEM)
			return;

	sz = req.size;
	if ((olh = calloc(1, sz)) == NULL)
		return;

	olh->size = sz;
	if (do_get3(IP_FW_DUMP_SRVOBJECTS, &olh->opheader, &sz) != 0) {
		free(olh);
		return;
	}

	if (olh->count > 0)
		printf("Objects list:\n");
	else
		printf("There are no objects\n");
	ntlv = (ipfw_obj_ntlv *)(olh + 1);
	for (i = 0; i < olh->count; i++) {
		name = match_value(otypes, ntlv->head.type);
		if (name == NULL)
			name = lookup_eaction_name(
			    (ipfw_obj_ntlv *)(olh + 1), olh->count,
			    ntlv->head.type);
		if (name == NULL)
			printf(" kidx: %4d\ttype: %10d\tname: %s\n",
			    ntlv->idx, ntlv->head.type, ntlv->name);
		else
			printf(" kidx: %4d\ttype: %10s\tname: %s\n",
			    ntlv->idx, name, ntlv->name);
		ntlv++;
	}
	free(olh);
}

void
ipfw_internal_handler(int ac, char *av[])
{
	int tcmd;

	ac--; av++;
	NEED1("internal cmd required");

	if ((tcmd = match_token(intcmds, *av)) == -1)
		errx(EX_USAGE, "invalid internal sub-cmd: %s", *av);

	switch (tcmd) {
	case TOK_IFLIST:
		ipfw_list_tifaces();
		break;
	case TOK_TALIST:
		ipfw_list_ta(ac, av);
		break;
	case TOK_OLIST:
		ipfw_list_objects(ac, av);
		break;
	case TOK_VLIST:
		ipfw_list_values(ac, av);
		break;
	}
}

static int
ipfw_get_tracked_ifaces(ipfw_obj_lheader **polh)
{
	ipfw_obj_lheader req, *olh;
	size_t sz;

	memset(&req, 0, sizeof(req));
	sz = sizeof(req);

	if (do_get3(IP_FW_XIFLIST, &req.opheader, &sz) != 0) {
		if (errno != ENOMEM)
			return (errno);
	}

	sz = req.size;
	if ((olh = calloc(1, sz)) == NULL)
		return (ENOMEM);

	olh->size = sz;
	if (do_get3(IP_FW_XIFLIST, &olh->opheader, &sz) != 0) {
		free(olh);
		return (errno);
	}

	*polh = olh;
	return (0);
}

static int
ifinfo_cmp(const void *a, const void *b)
{
	ipfw_iface_info *ia, *ib;

	ia = (ipfw_iface_info *)a;
	ib = (ipfw_iface_info *)b;

	return (stringnum_cmp(ia->ifname, ib->ifname));
}

/*
 * Retrieves table list from kernel,
 * optionally sorts it and calls requested function for each table.
 * Returns 0 on success.
 */
static void
ipfw_list_tifaces()
{
	ipfw_obj_lheader *olh;
	ipfw_iface_info *info;
	int i, error;

	if ((error = ipfw_get_tracked_ifaces(&olh)) != 0)
		err(EX_OSERR, "Unable to request ipfw tracked interface list");


	qsort(olh + 1, olh->count, olh->objsize, ifinfo_cmp);

	info = (ipfw_iface_info *)(olh + 1);
	for (i = 0; i < olh->count; i++) {
		if (info->flags & IPFW_IFFLAG_RESOLVED)
			printf("%s ifindex: %d refcount: %u changes: %u\n",
			    info->ifname, info->ifindex, info->refcnt,
			    info->gencnt);
		else
			printf("%s ifindex: unresolved refcount: %u changes: %u\n",
			    info->ifname, info->refcnt, info->gencnt);
		info = (ipfw_iface_info *)((caddr_t)info + olh->objsize);
	}

	free(olh);
}




