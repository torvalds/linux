/*
 * Minimal BPF debugger
 *
 * Minimal BPF debugger that mimics the kernel's engine (w/o extensions)
 * and allows for single stepping through selected packets from a pcap
 * with a provided user filter in order to facilitate verification of a
 * BPF program. Besides others, this is useful to verify BPF programs
 * before attaching to a live system, and can be used in socket filters,
 * cls_bpf, xt_bpf, team driver and e.g. PTP code; in particular when a
 * single more complex BPF program is being used. Reasons for a more
 * complex BPF program are likely primarily to optimize execution time
 * for making a verdict when multiple simple BPF programs are combined
 * into one in order to prevent parsing same headers multiple times.
 *
 * More on how to debug BPF opcodes see Documentation/networking/filter.txt
 * which is the main document on BPF. Mini howto for getting started:
 *
 *  1) `./bpf_dbg` to enter the shell (shell cmds denoted with '>'):
 *  2) > load bpf 6,40 0 0 12,21 0 3 20... (output from `bpf_asm` or
 *     `tcpdump -iem1 -ddd port 22 | tr '\n' ','` to load as filter)
 *  3) > load pcap foo.pcap
 *  4) > run <n>/disassemble/dump/quit (self-explanatory)
 *  5) > breakpoint 2 (sets bp at loaded BPF insns 2, do `run` then;
 *       multiple bps can be set, of course, a call to `breakpoint`
 *       w/o args shows currently loaded bps, `breakpoint reset` for
 *       resetting all breakpoints)
 *  6) > select 3 (`run` etc will start from the 3rd packet in the pcap)
 *  7) > step [-<n>, +<n>] (performs single stepping through the BPF)
 *
 * Copyright 2013 Daniel Borkmann <borkmann@redhat.com>
 * Licensed under the GNU General Public License, version 2.0 (GPLv2)
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <net/ethernet.h>

#define TCPDUMP_MAGIC	0xa1b2c3d4

#define BPF_LDX_B	(BPF_LDX | BPF_B)
#define BPF_LDX_W	(BPF_LDX | BPF_W)
#define BPF_JMP_JA	(BPF_JMP | BPF_JA)
#define BPF_JMP_JEQ	(BPF_JMP | BPF_JEQ)
#define BPF_JMP_JGT	(BPF_JMP | BPF_JGT)
#define BPF_JMP_JGE	(BPF_JMP | BPF_JGE)
#define BPF_JMP_JSET	(BPF_JMP | BPF_JSET)
#define BPF_ALU_ADD	(BPF_ALU | BPF_ADD)
#define BPF_ALU_SUB	(BPF_ALU | BPF_SUB)
#define BPF_ALU_MUL	(BPF_ALU | BPF_MUL)
#define BPF_ALU_DIV	(BPF_ALU | BPF_DIV)
#define BPF_ALU_MOD	(BPF_ALU | BPF_MOD)
#define BPF_ALU_NEG	(BPF_ALU | BPF_NEG)
#define BPF_ALU_AND	(BPF_ALU | BPF_AND)
#define BPF_ALU_OR	(BPF_ALU | BPF_OR)
#define BPF_ALU_XOR	(BPF_ALU | BPF_XOR)
#define BPF_ALU_LSH	(BPF_ALU | BPF_LSH)
#define BPF_ALU_RSH	(BPF_ALU | BPF_RSH)
#define BPF_MISC_TAX	(BPF_MISC | BPF_TAX)
#define BPF_MISC_TXA	(BPF_MISC | BPF_TXA)
#define BPF_LD_B	(BPF_LD | BPF_B)
#define BPF_LD_H	(BPF_LD | BPF_H)
#define BPF_LD_W	(BPF_LD | BPF_W)

#ifndef array_size
# define array_size(x)	(sizeof(x) / sizeof((x)[0]))
#endif

#ifndef __check_format_printf
# define __check_format_printf(pos_fmtstr, pos_fmtargs) \
	__attribute__ ((format (printf, (pos_fmtstr), (pos_fmtargs))))
#endif

enum {
	CMD_OK,
	CMD_ERR,
	CMD_EX,
};

struct shell_cmd {
	const char *name;
	int (*func)(char *args);
};

struct pcap_filehdr {
	uint32_t magic;
	uint16_t version_major;
	uint16_t version_minor;
	int32_t  thiszone;
	uint32_t sigfigs;
	uint32_t snaplen;
	uint32_t linktype;
};

struct pcap_timeval {
	int32_t tv_sec;
	int32_t tv_usec;
};

struct pcap_pkthdr {
	struct pcap_timeval ts;
	uint32_t caplen;
	uint32_t len;
};

struct bpf_regs {
	uint32_t A;
	uint32_t X;
	uint32_t M[BPF_MEMWORDS];
	uint32_t R;
	bool     Rs;
	uint16_t Pc;
};

static struct sock_filter bpf_image[BPF_MAXINSNS + 1];
static unsigned int bpf_prog_len;

static int bpf_breakpoints[64];
static struct bpf_regs bpf_regs[BPF_MAXINSNS + 1];
static struct bpf_regs bpf_curr;
static unsigned int bpf_regs_len;

static int pcap_fd = -1;
static unsigned int pcap_packet;
static size_t pcap_map_size;
static char *pcap_ptr_va_start, *pcap_ptr_va_curr;

static const char * const op_table[] = {
	[BPF_ST]	= "st",
	[BPF_STX]	= "stx",
	[BPF_LD_B]	= "ldb",
	[BPF_LD_H]	= "ldh",
	[BPF_LD_W]	= "ld",
	[BPF_LDX]	= "ldx",
	[BPF_LDX_B]	= "ldxb",
	[BPF_JMP_JA]	= "ja",
	[BPF_JMP_JEQ]	= "jeq",
	[BPF_JMP_JGT]	= "jgt",
	[BPF_JMP_JGE]	= "jge",
	[BPF_JMP_JSET]	= "jset",
	[BPF_ALU_ADD]	= "add",
	[BPF_ALU_SUB]	= "sub",
	[BPF_ALU_MUL]	= "mul",
	[BPF_ALU_DIV]	= "div",
	[BPF_ALU_MOD]	= "mod",
	[BPF_ALU_NEG]	= "neg",
	[BPF_ALU_AND]	= "and",
	[BPF_ALU_OR]	= "or",
	[BPF_ALU_XOR]	= "xor",
	[BPF_ALU_LSH]	= "lsh",
	[BPF_ALU_RSH]	= "rsh",
	[BPF_MISC_TAX]	= "tax",
	[BPF_MISC_TXA]	= "txa",
	[BPF_RET]	= "ret",
};

static __check_format_printf(1, 2) int rl_printf(const char *fmt, ...)
{
	int ret;
	va_list vl;

	va_start(vl, fmt);
	ret = vfprintf(rl_outstream, fmt, vl);
	va_end(vl);

	return ret;
}

static int matches(const char *cmd, const char *pattern)
{
	int len = strlen(cmd);

	if (len > strlen(pattern))
		return -1;

	return memcmp(pattern, cmd, len);
}

static void hex_dump(const uint8_t *buf, size_t len)
{
	int i;

	rl_printf("%3u: ", 0);
	for (i = 0; i < len; i++) {
		if (i && !(i % 16))
			rl_printf("\n%3u: ", i);
		rl_printf("%02x ", buf[i]);
	}
	rl_printf("\n");
}

static bool bpf_prog_loaded(void)
{
	if (bpf_prog_len == 0)
		rl_printf("no bpf program loaded!\n");

	return bpf_prog_len > 0;
}

static void bpf_disasm(const struct sock_filter f, unsigned int i)
{
	const char *op, *fmt;
	int val = f.k;
	char buf[256];

	switch (f.code) {
	case BPF_RET | BPF_K:
		op = op_table[BPF_RET];
		fmt = "#%#x";
		break;
	case BPF_RET | BPF_A:
		op = op_table[BPF_RET];
		fmt = "a";
		break;
	case BPF_RET | BPF_X:
		op = op_table[BPF_RET];
		fmt = "x";
		break;
	case BPF_MISC_TAX:
		op = op_table[BPF_MISC_TAX];
		fmt = "";
		break;
	case BPF_MISC_TXA:
		op = op_table[BPF_MISC_TXA];
		fmt = "";
		break;
	case BPF_ST:
		op = op_table[BPF_ST];
		fmt = "M[%d]";
		break;
	case BPF_STX:
		op = op_table[BPF_STX];
		fmt = "M[%d]";
		break;
	case BPF_LD_W | BPF_ABS:
		op = op_table[BPF_LD_W];
		fmt = "[%d]";
		break;
	case BPF_LD_H | BPF_ABS:
		op = op_table[BPF_LD_H];
		fmt = "[%d]";
		break;
	case BPF_LD_B | BPF_ABS:
		op = op_table[BPF_LD_B];
		fmt = "[%d]";
		break;
	case BPF_LD_W | BPF_LEN:
		op = op_table[BPF_LD_W];
		fmt = "#len";
		break;
	case BPF_LD_W | BPF_IND:
		op = op_table[BPF_LD_W];
		fmt = "[x+%d]";
		break;
	case BPF_LD_H | BPF_IND:
		op = op_table[BPF_LD_H];
		fmt = "[x+%d]";
		break;
	case BPF_LD_B | BPF_IND:
		op = op_table[BPF_LD_B];
		fmt = "[x+%d]";
		break;
	case BPF_LD | BPF_IMM:
		op = op_table[BPF_LD_W];
		fmt = "#%#x";
		break;
	case BPF_LDX | BPF_IMM:
		op = op_table[BPF_LDX];
		fmt = "#%#x";
		break;
	case BPF_LDX_B | BPF_MSH:
		op = op_table[BPF_LDX_B];
		fmt = "4*([%d]&0xf)";
		break;
	case BPF_LD | BPF_MEM:
		op = op_table[BPF_LD_W];
		fmt = "M[%d]";
		break;
	case BPF_LDX | BPF_MEM:
		op = op_table[BPF_LDX];
		fmt = "M[%d]";
		break;
	case BPF_JMP_JA:
		op = op_table[BPF_JMP_JA];
		fmt = "%d";
		val = i + 1 + f.k;
		break;
	case BPF_JMP_JGT | BPF_X:
		op = op_table[BPF_JMP_JGT];
		fmt = "x";
		break;
	case BPF_JMP_JGT | BPF_K:
		op = op_table[BPF_JMP_JGT];
		fmt = "#%#x";
		break;
	case BPF_JMP_JGE | BPF_X:
		op = op_table[BPF_JMP_JGE];
		fmt = "x";
		break;
	case BPF_JMP_JGE | BPF_K:
		op = op_table[BPF_JMP_JGE];
		fmt = "#%#x";
		break;
	case BPF_JMP_JEQ | BPF_X:
		op = op_table[BPF_JMP_JEQ];
		fmt = "x";
		break;
	case BPF_JMP_JEQ | BPF_K:
		op = op_table[BPF_JMP_JEQ];
		fmt = "#%#x";
		break;
	case BPF_JMP_JSET | BPF_X:
		op = op_table[BPF_JMP_JSET];
		fmt = "x";
		break;
	case BPF_JMP_JSET | BPF_K:
		op = op_table[BPF_JMP_JSET];
		fmt = "#%#x";
		break;
	case BPF_ALU_NEG:
		op = op_table[BPF_ALU_NEG];
		fmt = "";
		break;
	case BPF_ALU_LSH | BPF_X:
		op = op_table[BPF_ALU_LSH];
		fmt = "x";
		break;
	case BPF_ALU_LSH | BPF_K:
		op = op_table[BPF_ALU_LSH];
		fmt = "#%d";
		break;
	case BPF_ALU_RSH | BPF_X:
		op = op_table[BPF_ALU_RSH];
		fmt = "x";
		break;
	case BPF_ALU_RSH | BPF_K:
		op = op_table[BPF_ALU_RSH];
		fmt = "#%d";
		break;
	case BPF_ALU_ADD | BPF_X:
		op = op_table[BPF_ALU_ADD];
		fmt = "x";
		break;
	case BPF_ALU_ADD | BPF_K:
		op = op_table[BPF_ALU_ADD];
		fmt = "#%d";
		break;
	case BPF_ALU_SUB | BPF_X:
		op = op_table[BPF_ALU_SUB];
		fmt = "x";
		break;
	case BPF_ALU_SUB | BPF_K:
		op = op_table[BPF_ALU_SUB];
		fmt = "#%d";
		break;
	case BPF_ALU_MUL | BPF_X:
		op = op_table[BPF_ALU_MUL];
		fmt = "x";
		break;
	case BPF_ALU_MUL | BPF_K:
		op = op_table[BPF_ALU_MUL];
		fmt = "#%d";
		break;
	case BPF_ALU_DIV | BPF_X:
		op = op_table[BPF_ALU_DIV];
		fmt = "x";
		break;
	case BPF_ALU_DIV | BPF_K:
		op = op_table[BPF_ALU_DIV];
		fmt = "#%d";
		break;
	case BPF_ALU_MOD | BPF_X:
		op = op_table[BPF_ALU_MOD];
		fmt = "x";
		break;
	case BPF_ALU_MOD | BPF_K:
		op = op_table[BPF_ALU_MOD];
		fmt = "#%d";
		break;
	case BPF_ALU_AND | BPF_X:
		op = op_table[BPF_ALU_AND];
		fmt = "x";
		break;
	case BPF_ALU_AND | BPF_K:
		op = op_table[BPF_ALU_AND];
		fmt = "#%#x";
		break;
	case BPF_ALU_OR | BPF_X:
		op = op_table[BPF_ALU_OR];
		fmt = "x";
		break;
	case BPF_ALU_OR | BPF_K:
		op = op_table[BPF_ALU_OR];
		fmt = "#%#x";
		break;
	case BPF_ALU_XOR | BPF_X:
		op = op_table[BPF_ALU_XOR];
		fmt = "x";
		break;
	case BPF_ALU_XOR | BPF_K:
		op = op_table[BPF_ALU_XOR];
		fmt = "#%#x";
		break;
	default:
		op = "nosup";
		fmt = "%#x";
		val = f.code;
		break;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), fmt, val);
	buf[sizeof(buf) - 1] = 0;

	if ((BPF_CLASS(f.code) == BPF_JMP && BPF_OP(f.code) != BPF_JA))
		rl_printf("l%d:\t%s %s, l%d, l%d\n", i, op, buf,
			  i + 1 + f.jt, i + 1 + f.jf);
	else
		rl_printf("l%d:\t%s %s\n", i, op, buf);
}

static void bpf_dump_curr(struct bpf_regs *r, struct sock_filter *f)
{
	int i, m = 0;

	rl_printf("pc:       [%u]\n", r->Pc);
	rl_printf("code:     [%u] jt[%u] jf[%u] k[%u]\n",
		  f->code, f->jt, f->jf, f->k);
	rl_printf("curr:     ");
	bpf_disasm(*f, r->Pc);

	if (f->jt || f->jf) {
		rl_printf("jt:       ");
		bpf_disasm(*(f + f->jt + 1), r->Pc + f->jt + 1);
		rl_printf("jf:       ");
		bpf_disasm(*(f + f->jf + 1), r->Pc + f->jf + 1);
	}

	rl_printf("A:        [%#08x][%u]\n", r->A, r->A);
	rl_printf("X:        [%#08x][%u]\n", r->X, r->X);
	if (r->Rs)
		rl_printf("ret:      [%#08x][%u]!\n", r->R, r->R);

	for (i = 0; i < BPF_MEMWORDS; i++) {
		if (r->M[i]) {
			m++;
			rl_printf("M[%d]: [%#08x][%u]\n", i, r->M[i], r->M[i]);
		}
	}
	if (m == 0)
		rl_printf("M[0,%d]:  [%#08x][%u]\n", BPF_MEMWORDS - 1, 0, 0);
}

static void bpf_dump_pkt(uint8_t *pkt, uint32_t pkt_caplen, uint32_t pkt_len)
{
	if (pkt_caplen != pkt_len)
		rl_printf("cap: %u, len: %u\n", pkt_caplen, pkt_len);
	else
		rl_printf("len: %u\n", pkt_len);

	hex_dump(pkt, pkt_caplen);
}

static void bpf_disasm_all(const struct sock_filter *f, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++)
		bpf_disasm(f[i], i);
}

static void bpf_dump_all(const struct sock_filter *f, unsigned int len)
{
	unsigned int i;

	rl_printf("/* { op, jt, jf, k }, */\n");
	for (i = 0; i < len; i++)
		rl_printf("{ %#04x, %2u, %2u, %#010x },\n",
			  f[i].code, f[i].jt, f[i].jf, f[i].k);
}

static bool bpf_runnable(struct sock_filter *f, unsigned int len)
{
	int sock, ret, i;
	struct sock_fprog bpf = {
		.filter = f,
		.len = len,
	};

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		rl_printf("cannot open socket!\n");
		return false;
	}
	ret = setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf));
	close(sock);
	if (ret < 0) {
		rl_printf("program not allowed to run by kernel!\n");
		return false;
	}
	for (i = 0; i < len; i++) {
		if (BPF_CLASS(f[i].code) == BPF_LD &&
		    f[i].k > SKF_AD_OFF) {
			rl_printf("extensions currently not supported!\n");
			return false;
		}
	}

	return true;
}

static void bpf_reset_breakpoints(void)
{
	int i;

	for (i = 0; i < array_size(bpf_breakpoints); i++)
		bpf_breakpoints[i] = -1;
}

static void bpf_set_breakpoints(unsigned int where)
{
	int i;
	bool set = false;

	for (i = 0; i < array_size(bpf_breakpoints); i++) {
		if (bpf_breakpoints[i] == (int) where) {
			rl_printf("breakpoint already set!\n");
			set = true;
			break;
		}

		if (bpf_breakpoints[i] == -1 && set == false) {
			bpf_breakpoints[i] = where;
			set = true;
		}
	}

	if (!set)
		rl_printf("too many breakpoints set, reset first!\n");
}

static void bpf_dump_breakpoints(void)
{
	int i;

	rl_printf("breakpoints: ");

	for (i = 0; i < array_size(bpf_breakpoints); i++) {
		if (bpf_breakpoints[i] < 0)
			continue;
		rl_printf("%d ", bpf_breakpoints[i]);
	}

	rl_printf("\n");
}

static void bpf_reset(void)
{
	bpf_regs_len = 0;

	memset(bpf_regs, 0, sizeof(bpf_regs));
	memset(&bpf_curr, 0, sizeof(bpf_curr));
}

static void bpf_safe_regs(void)
{
	memcpy(&bpf_regs[bpf_regs_len++], &bpf_curr, sizeof(bpf_curr));
}

static bool bpf_restore_regs(int off)
{
	unsigned int index = bpf_regs_len - 1 + off;

	if (index == 0) {
		bpf_reset();
		return true;
	} else if (index < bpf_regs_len) {
		memcpy(&bpf_curr, &bpf_regs[index], sizeof(bpf_curr));
		bpf_regs_len = index;
		return true;
	} else {
		rl_printf("reached bottom of register history stack!\n");
		return false;
	}
}

static uint32_t extract_u32(uint8_t *pkt, uint32_t off)
{
	uint32_t r;

	memcpy(&r, &pkt[off], sizeof(r));

	return ntohl(r);
}

static uint16_t extract_u16(uint8_t *pkt, uint32_t off)
{
	uint16_t r;

	memcpy(&r, &pkt[off], sizeof(r));

	return ntohs(r);
}

static uint8_t extract_u8(uint8_t *pkt, uint32_t off)
{
	return pkt[off];
}

static void set_return(struct bpf_regs *r)
{
	r->R = 0;
	r->Rs = true;
}

static void bpf_single_step(struct bpf_regs *r, struct sock_filter *f,
			    uint8_t *pkt, uint32_t pkt_caplen,
			    uint32_t pkt_len)
{
	uint32_t K = f->k;
	int d;

	switch (f->code) {
	case BPF_RET | BPF_K:
		r->R = K;
		r->Rs = true;
		break;
	case BPF_RET | BPF_A:
		r->R = r->A;
		r->Rs = true;
		break;
	case BPF_RET | BPF_X:
		r->R = r->X;
		r->Rs = true;
		break;
	case BPF_MISC_TAX:
		r->X = r->A;
		break;
	case BPF_MISC_TXA:
		r->A = r->X;
		break;
	case BPF_ST:
		r->M[K] = r->A;
		break;
	case BPF_STX:
		r->M[K] = r->X;
		break;
	case BPF_LD_W | BPF_ABS:
		d = pkt_caplen - K;
		if (d >= sizeof(uint32_t))
			r->A = extract_u32(pkt, K);
		else
			set_return(r);
		break;
	case BPF_LD_H | BPF_ABS:
		d = pkt_caplen - K;
		if (d >= sizeof(uint16_t))
			r->A = extract_u16(pkt, K);
		else
			set_return(r);
		break;
	case BPF_LD_B | BPF_ABS:
		d = pkt_caplen - K;
		if (d >= sizeof(uint8_t))
			r->A = extract_u8(pkt, K);
		else
			set_return(r);
		break;
	case BPF_LD_W | BPF_IND:
		d = pkt_caplen - (r->X + K);
		if (d >= sizeof(uint32_t))
			r->A = extract_u32(pkt, r->X + K);
		break;
	case BPF_LD_H | BPF_IND:
		d = pkt_caplen - (r->X + K);
		if (d >= sizeof(uint16_t))
			r->A = extract_u16(pkt, r->X + K);
		else
			set_return(r);
		break;
	case BPF_LD_B | BPF_IND:
		d = pkt_caplen - (r->X + K);
		if (d >= sizeof(uint8_t))
			r->A = extract_u8(pkt, r->X + K);
		else
			set_return(r);
		break;
	case BPF_LDX_B | BPF_MSH:
		d = pkt_caplen - K;
		if (d >= sizeof(uint8_t)) {
			r->X = extract_u8(pkt, K);
			r->X = (r->X & 0xf) << 2;
		} else
			set_return(r);
		break;
	case BPF_LD_W | BPF_LEN:
		r->A = pkt_len;
		break;
	case BPF_LDX_W | BPF_LEN:
		r->A = pkt_len;
		break;
	case BPF_LD | BPF_IMM:
		r->A = K;
		break;
	case BPF_LDX | BPF_IMM:
		r->X = K;
		break;
	case BPF_LD | BPF_MEM:
		r->A = r->M[K];
		break;
	case BPF_LDX | BPF_MEM:
		r->X = r->M[K];
		break;
	case BPF_JMP_JA:
		r->Pc += K;
		break;
	case BPF_JMP_JGT | BPF_X:
		r->Pc += r->A > r->X ? f->jt : f->jf;
		break;
	case BPF_JMP_JGT | BPF_K:
		r->Pc += r->A > K ? f->jt : f->jf;
		break;
	case BPF_JMP_JGE | BPF_X:
		r->Pc += r->A >= r->X ? f->jt : f->jf;
		break;
	case BPF_JMP_JGE | BPF_K:
		r->Pc += r->A >= K ? f->jt : f->jf;
		break;
	case BPF_JMP_JEQ | BPF_X:
		r->Pc += r->A == r->X ? f->jt : f->jf;
		break;
	case BPF_JMP_JEQ | BPF_K:
		r->Pc += r->A == K ? f->jt : f->jf;
		break;
	case BPF_JMP_JSET | BPF_X:
		r->Pc += r->A & r->X ? f->jt : f->jf;
		break;
	case BPF_JMP_JSET | BPF_K:
		r->Pc += r->A & K ? f->jt : f->jf;
		break;
	case BPF_ALU_NEG:
		r->A = -r->A;
		break;
	case BPF_ALU_LSH | BPF_X:
		r->A <<= r->X;
		break;
	case BPF_ALU_LSH | BPF_K:
		r->A <<= K;
		break;
	case BPF_ALU_RSH | BPF_X:
		r->A >>= r->X;
		break;
	case BPF_ALU_RSH | BPF_K:
		r->A >>= K;
		break;
	case BPF_ALU_ADD | BPF_X:
		r->A += r->X;
		break;
	case BPF_ALU_ADD | BPF_K:
		r->A += K;
		break;
	case BPF_ALU_SUB | BPF_X:
		r->A -= r->X;
		break;
	case BPF_ALU_SUB | BPF_K:
		r->A -= K;
		break;
	case BPF_ALU_MUL | BPF_X:
		r->A *= r->X;
		break;
	case BPF_ALU_MUL | BPF_K:
		r->A *= K;
		break;
	case BPF_ALU_DIV | BPF_X:
	case BPF_ALU_MOD | BPF_X:
		if (r->X == 0) {
			set_return(r);
			break;
		}
		goto do_div;
	case BPF_ALU_DIV | BPF_K:
	case BPF_ALU_MOD | BPF_K:
		if (K == 0) {
			set_return(r);
			break;
		}
do_div:
		switch (f->code) {
		case BPF_ALU_DIV | BPF_X:
			r->A /= r->X;
			break;
		case BPF_ALU_DIV | BPF_K:
			r->A /= K;
			break;
		case BPF_ALU_MOD | BPF_X:
			r->A %= r->X;
			break;
		case BPF_ALU_MOD | BPF_K:
			r->A %= K;
			break;
		}
		break;
	case BPF_ALU_AND | BPF_X:
		r->A &= r->X;
		break;
	case BPF_ALU_AND | BPF_K:
		r->A &= K;
		break;
	case BPF_ALU_OR | BPF_X:
		r->A |= r->X;
		break;
	case BPF_ALU_OR | BPF_K:
		r->A |= K;
		break;
	case BPF_ALU_XOR | BPF_X:
		r->A ^= r->X;
		break;
	case BPF_ALU_XOR | BPF_K:
		r->A ^= K;
		break;
	}
}

static bool bpf_pc_has_breakpoint(uint16_t pc)
{
	int i;

	for (i = 0; i < array_size(bpf_breakpoints); i++) {
		if (bpf_breakpoints[i] < 0)
			continue;
		if (bpf_breakpoints[i] == pc)
			return true;
	}

	return false;
}

static bool bpf_handle_breakpoint(struct bpf_regs *r, struct sock_filter *f,
				  uint8_t *pkt, uint32_t pkt_caplen,
				  uint32_t pkt_len)
{
	rl_printf("-- register dump --\n");
	bpf_dump_curr(r, &f[r->Pc]);
	rl_printf("-- packet dump --\n");
	bpf_dump_pkt(pkt, pkt_caplen, pkt_len);
	rl_printf("(breakpoint)\n");
	return true;
}

static int bpf_run_all(struct sock_filter *f, uint16_t bpf_len, uint8_t *pkt,
		       uint32_t pkt_caplen, uint32_t pkt_len)
{
	bool stop = false;

	while (bpf_curr.Rs == false && stop == false) {
		bpf_safe_regs();

		if (bpf_pc_has_breakpoint(bpf_curr.Pc))
			stop = bpf_handle_breakpoint(&bpf_curr, f, pkt,
						     pkt_caplen, pkt_len);

		bpf_single_step(&bpf_curr, &f[bpf_curr.Pc], pkt, pkt_caplen,
				pkt_len);
		bpf_curr.Pc++;
	}

	return stop ? -1 : bpf_curr.R;
}

static int bpf_run_stepping(struct sock_filter *f, uint16_t bpf_len,
			    uint8_t *pkt, uint32_t pkt_caplen,
			    uint32_t pkt_len, int next)
{
	bool stop = false;
	int i = 1;

	while (bpf_curr.Rs == false && stop == false) {
		bpf_safe_regs();

		if (i++ == next)
			stop = bpf_handle_breakpoint(&bpf_curr, f, pkt,
						     pkt_caplen, pkt_len);

		bpf_single_step(&bpf_curr, &f[bpf_curr.Pc], pkt, pkt_caplen,
				pkt_len);
		bpf_curr.Pc++;
	}

	return stop ? -1 : bpf_curr.R;
}

static bool pcap_loaded(void)
{
	if (pcap_fd < 0)
		rl_printf("no pcap file loaded!\n");

	return pcap_fd >= 0;
}

static struct pcap_pkthdr *pcap_curr_pkt(void)
{
	return (void *) pcap_ptr_va_curr;
}

static bool pcap_next_pkt(void)
{
	struct pcap_pkthdr *hdr = pcap_curr_pkt();

	if (pcap_ptr_va_curr + sizeof(*hdr) -
	    pcap_ptr_va_start >= pcap_map_size)
		return false;
	if (hdr->caplen == 0 || hdr->len == 0 || hdr->caplen > hdr->len)
		return false;
	if (pcap_ptr_va_curr + sizeof(*hdr) + hdr->caplen -
	    pcap_ptr_va_start >= pcap_map_size)
		return false;

	pcap_ptr_va_curr += (sizeof(*hdr) + hdr->caplen);
	return true;
}

static void pcap_reset_pkt(void)
{
	pcap_ptr_va_curr = pcap_ptr_va_start + sizeof(struct pcap_filehdr);
}

static int try_load_pcap(const char *file)
{
	struct pcap_filehdr *hdr;
	struct stat sb;
	int ret;

	pcap_fd = open(file, O_RDONLY);
	if (pcap_fd < 0) {
		rl_printf("cannot open pcap [%s]!\n", strerror(errno));
		return CMD_ERR;
	}

	ret = fstat(pcap_fd, &sb);
	if (ret < 0) {
		rl_printf("cannot fstat pcap file!\n");
		return CMD_ERR;
	}

	if (!S_ISREG(sb.st_mode)) {
		rl_printf("not a regular pcap file, duh!\n");
		return CMD_ERR;
	}

	pcap_map_size = sb.st_size;
	if (pcap_map_size <= sizeof(struct pcap_filehdr)) {
		rl_printf("pcap file too small!\n");
		return CMD_ERR;
	}

	pcap_ptr_va_start = mmap(NULL, pcap_map_size, PROT_READ,
				 MAP_SHARED | MAP_LOCKED, pcap_fd, 0);
	if (pcap_ptr_va_start == MAP_FAILED) {
		rl_printf("mmap of file failed!");
		return CMD_ERR;
	}

	hdr = (void *) pcap_ptr_va_start;
	if (hdr->magic != TCPDUMP_MAGIC) {
		rl_printf("wrong pcap magic!\n");
		return CMD_ERR;
	}

	pcap_reset_pkt();

	return CMD_OK;

}

static void try_close_pcap(void)
{
	if (pcap_fd >= 0) {
		munmap(pcap_ptr_va_start, pcap_map_size);
		close(pcap_fd);

		pcap_ptr_va_start = pcap_ptr_va_curr = NULL;
		pcap_map_size = 0;
		pcap_packet = 0;
		pcap_fd = -1;
	}
}

static int cmd_load_bpf(char *bpf_string)
{
	char sp, *token, separator = ',';
	unsigned short bpf_len, i = 0;
	struct sock_filter tmp;

	bpf_prog_len = 0;
	memset(bpf_image, 0, sizeof(bpf_image));

	if (sscanf(bpf_string, "%hu%c", &bpf_len, &sp) != 2 ||
	    sp != separator || bpf_len > BPF_MAXINSNS || bpf_len == 0) {
		rl_printf("syntax error in head length encoding!\n");
		return CMD_ERR;
	}

	token = bpf_string;
	while ((token = strchr(token, separator)) && (++token)[0]) {
		if (i >= bpf_len) {
			rl_printf("program exceeds encoded length!\n");
			return CMD_ERR;
		}

		if (sscanf(token, "%hu %hhu %hhu %u,",
			   &tmp.code, &tmp.jt, &tmp.jf, &tmp.k) != 4) {
			rl_printf("syntax error at instruction %d!\n", i);
			return CMD_ERR;
		}

		bpf_image[i].code = tmp.code;
		bpf_image[i].jt = tmp.jt;
		bpf_image[i].jf = tmp.jf;
		bpf_image[i].k = tmp.k;

		i++;
	}

	if (i != bpf_len) {
		rl_printf("syntax error exceeding encoded length!\n");
		return CMD_ERR;
	} else
		bpf_prog_len = bpf_len;
	if (!bpf_runnable(bpf_image, bpf_prog_len))
		bpf_prog_len = 0;

	return CMD_OK;
}

static int cmd_load_pcap(char *file)
{
	char *file_trim, *tmp;

	file_trim = strtok_r(file, " ", &tmp);
	if (file_trim == NULL)
		return CMD_ERR;

	try_close_pcap();

	return try_load_pcap(file_trim);
}

static int cmd_load(char *arg)
{
	char *subcmd, *cont = NULL, *tmp = strdup(arg);
	int ret = CMD_OK;

	subcmd = strtok_r(tmp, " ", &cont);
	if (subcmd == NULL)
		goto out;
	if (matches(subcmd, "bpf") == 0) {
		bpf_reset();
		bpf_reset_breakpoints();

		if (!cont)
			ret = CMD_ERR;
		else
			ret = cmd_load_bpf(cont);
	} else if (matches(subcmd, "pcap") == 0) {
		ret = cmd_load_pcap(cont);
	} else {
out:
		rl_printf("bpf <code>:  load bpf code\n");
		rl_printf("pcap <file>: load pcap file\n");
		ret = CMD_ERR;
	}

	free(tmp);
	return ret;
}

static int cmd_step(char *num)
{
	struct pcap_pkthdr *hdr;
	int steps, ret;

	if (!bpf_prog_loaded() || !pcap_loaded())
		return CMD_ERR;

	steps = strtol(num, NULL, 10);
	if (steps == 0 || strlen(num) == 0)
		steps = 1;
	if (steps < 0) {
		if (!bpf_restore_regs(steps))
			return CMD_ERR;
		steps = 1;
	}

	hdr = pcap_curr_pkt();
	ret = bpf_run_stepping(bpf_image, bpf_prog_len,
			       (uint8_t *) hdr + sizeof(*hdr),
			       hdr->caplen, hdr->len, steps);
	if (ret >= 0 || bpf_curr.Rs) {
		bpf_reset();
		if (!pcap_next_pkt()) {
			rl_printf("(going back to first packet)\n");
			pcap_reset_pkt();
		} else {
			rl_printf("(next packet)\n");
		}
	}

	return CMD_OK;
}

static int cmd_select(char *num)
{
	unsigned int which, i;
	bool have_next = true;

	if (!pcap_loaded() || strlen(num) == 0)
		return CMD_ERR;

	which = strtoul(num, NULL, 10);
	if (which == 0) {
		rl_printf("packet count starts with 1, clamping!\n");
		which = 1;
	}

	pcap_reset_pkt();
	bpf_reset();

	for (i = 0; i < which && (have_next = pcap_next_pkt()); i++)
		/* noop */;
	if (!have_next || pcap_curr_pkt() == NULL) {
		rl_printf("no packet #%u available!\n", which);
		pcap_reset_pkt();
		return CMD_ERR;
	}

	return CMD_OK;
}

static int cmd_breakpoint(char *subcmd)
{
	if (!bpf_prog_loaded())
		return CMD_ERR;
	if (strlen(subcmd) == 0)
		bpf_dump_breakpoints();
	else if (matches(subcmd, "reset") == 0)
		bpf_reset_breakpoints();
	else {
		unsigned int where = strtoul(subcmd, NULL, 10);

		if (where < bpf_prog_len) {
			bpf_set_breakpoints(where);
			rl_printf("breakpoint at: ");
			bpf_disasm(bpf_image[where], where);
		}
	}

	return CMD_OK;
}

static int cmd_run(char *num)
{
	static uint32_t pass, fail;
	bool has_limit = true;
	int pkts = 0, i = 0;

	if (!bpf_prog_loaded() || !pcap_loaded())
		return CMD_ERR;

	pkts = strtol(num, NULL, 10);
	if (pkts == 0 || strlen(num) == 0)
		has_limit = false;

	do {
		struct pcap_pkthdr *hdr = pcap_curr_pkt();
		int ret = bpf_run_all(bpf_image, bpf_prog_len,
				      (uint8_t *) hdr + sizeof(*hdr),
				      hdr->caplen, hdr->len);
		if (ret > 0)
			pass++;
		else if (ret == 0)
			fail++;
		else
			return CMD_OK;
		bpf_reset();
	} while (pcap_next_pkt() && (!has_limit || (has_limit && ++i < pkts)));

	rl_printf("bpf passes:%u fails:%u\n", pass, fail);

	pcap_reset_pkt();
	bpf_reset();

	pass = fail = 0;
	return CMD_OK;
}

static int cmd_disassemble(char *line_string)
{
	bool single_line = false;
	unsigned long line;

	if (!bpf_prog_loaded())
		return CMD_ERR;
	if (strlen(line_string) > 0 &&
	    (line = strtoul(line_string, NULL, 10)) < bpf_prog_len)
		single_line = true;
	if (single_line)
		bpf_disasm(bpf_image[line], line);
	else
		bpf_disasm_all(bpf_image, bpf_prog_len);

	return CMD_OK;
}

static int cmd_dump(char *dontcare)
{
	if (!bpf_prog_loaded())
		return CMD_ERR;

	bpf_dump_all(bpf_image, bpf_prog_len);

	return CMD_OK;
}

static int cmd_quit(char *dontcare)
{
	return CMD_EX;
}

static const struct shell_cmd cmds[] = {
	{ .name = "load", .func = cmd_load },
	{ .name = "select", .func = cmd_select },
	{ .name = "step", .func = cmd_step },
	{ .name = "run", .func = cmd_run },
	{ .name = "breakpoint", .func = cmd_breakpoint },
	{ .name = "disassemble", .func = cmd_disassemble },
	{ .name = "dump", .func = cmd_dump },
	{ .name = "quit", .func = cmd_quit },
};

static int execf(char *arg)
{
	char *cmd, *cont, *tmp = strdup(arg);
	int i, ret = 0, len;

	cmd = strtok_r(tmp, " ", &cont);
	if (cmd == NULL)
		goto out;
	len = strlen(cmd);
	for (i = 0; i < array_size(cmds); i++) {
		if (len != strlen(cmds[i].name))
			continue;
		if (strncmp(cmds[i].name, cmd, len) == 0) {
			ret = cmds[i].func(cont);
			break;
		}
	}
out:
	free(tmp);
	return ret;
}

static char *shell_comp_gen(const char *buf, int state)
{
	static int list_index, len;

	if (!state) {
		list_index = 0;
		len = strlen(buf);
	}

	for (; list_index < array_size(cmds); ) {
		const char *name = cmds[list_index].name;

		list_index++;
		if (strncmp(name, buf, len) == 0)
			return strdup(name);
	}

	return NULL;
}

static char **shell_completion(const char *buf, int start, int end)
{
	char **matches = NULL;

	if (start == 0)
		matches = rl_completion_matches(buf, shell_comp_gen);

	return matches;
}

static void intr_shell(int sig)
{
	if (rl_end)
		rl_kill_line(-1, 0);

	rl_crlf();
	rl_refresh_line(0, 0);
	rl_free_line_state();
}

static void init_shell(FILE *fin, FILE *fout)
{
	char file[128];

	snprintf(file, sizeof(file), "%s/.bpf_dbg_history", getenv("HOME"));
	read_history(file);

	rl_instream = fin;
	rl_outstream = fout;

	rl_readline_name = "bpf_dbg";
	rl_terminal_name = getenv("TERM");

	rl_catch_signals = 0;
	rl_catch_sigwinch = 1;

	rl_attempted_completion_function = shell_completion;

	rl_bind_key('\t', rl_complete);

	rl_bind_key_in_map('\t', rl_complete, emacs_meta_keymap);
	rl_bind_key_in_map('\033', rl_complete, emacs_meta_keymap);

	snprintf(file, sizeof(file), "%s/.bpf_dbg_init", getenv("HOME"));
	rl_read_init_file(file);

	rl_prep_terminal(0);
	rl_set_signals();

	signal(SIGINT, intr_shell);
}

static void exit_shell(FILE *fin, FILE *fout)
{
	char file[128];

	snprintf(file, sizeof(file), "%s/.bpf_dbg_history", getenv("HOME"));
	write_history(file);

	clear_history();
	rl_deprep_terminal();

	try_close_pcap();

	if (fin != stdin)
		fclose(fin);
	if (fout != stdout)
		fclose(fout);
}

static int run_shell_loop(FILE *fin, FILE *fout)
{
	char *buf;

	init_shell(fin, fout);

	while ((buf = readline("> ")) != NULL) {
		int ret = execf(buf);
		if (ret == CMD_EX)
			break;
		if (ret == CMD_OK && strlen(buf) > 0)
			add_history(buf);

		free(buf);
	}

	exit_shell(fin, fout);
	return 0;
}

int main(int argc, char **argv)
{
	FILE *fin = NULL, *fout = NULL;

	if (argc >= 2)
		fin = fopen(argv[1], "r");
	if (argc >= 3)
		fout = fopen(argv[2], "w");

	return run_shell_loop(fin ? : stdin, fout ? : stdout);
}
